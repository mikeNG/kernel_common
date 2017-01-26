/*
 *  MMC Oops/Panic logger
 *
 *  Copyright (C) 2010-2015 Samsung Electronics
 *  Jaehoon Chung <jh80.chung@samsung.com>
 *  Kyungmin Park <kyungmin.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kmsg_dump.h>
#include <linux/slab.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/scatterlist.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#define RESULT_OK		0
#define RESULT_FAIL		1
#define RESULT_UNSUP_HOST	2
#define RESULT_UNSUP_CARD	3

#define BUFFER_SIZE		512000

static int dump_oops = 1;
module_param(dump_oops, int, 0600);
MODULE_PARM_DESC(dump_oops,
		"set to 1 to dump oopses, 0 to only dump panics (default 1)");

static int trigger_dump(const char *str, struct kernel_param *kp)
{
	kmsg_dump(KMSG_DUMP_OOPS);
	return 0;
}
module_param_call(trigger_dump, trigger_dump, NULL, NULL, 0600);

#define dev_to_mmc_card(d)	container_of(d, struct mmc_card, dev)

static struct mmcoops_context {
	struct kmsg_dumper	dump;
	struct mmc_card		*card;

	unsigned int		start_offset_blk;
	unsigned int		size_bytes;

	struct device		*dev;
	struct platform_device	*pdev;

	u8			*buffer;
} oops_cxt;

/*******************************************************************/
/*  General helper functions                                       */
/*******************************************************************/

/*
 * Configure correct block size in card
 */
static int mmc_test_set_blksize(struct mmcoops_context *test, unsigned size)
{
	return mmc_set_blocklen(test->card, size);
}

/*
 * Fill in the mmc_request structure given a set of transfer parameters.
 */
static void mmc_test_prepare_mrq(struct mmcoops_context *test,
	struct mmc_request *mrq, struct scatterlist *sg, unsigned sg_len,
	unsigned dev_addr, unsigned blocks, unsigned blksz, int write)
{
	BUG_ON(!mrq || !mrq->cmd || !mrq->data || !mrq->stop);

	if (blocks > 1) {
		mrq->cmd->opcode = write ?
			MMC_WRITE_MULTIPLE_BLOCK : MMC_READ_MULTIPLE_BLOCK;
	} else {
		mrq->cmd->opcode = write ?
			MMC_WRITE_BLOCK : MMC_READ_SINGLE_BLOCK;
	}

	mrq->cmd->arg = dev_addr;
	if (!mmc_card_blockaddr(test->card))
		mrq->cmd->arg <<= 9;

	mrq->cmd->flags = MMC_RSP_R1 | MMC_CMD_ADTC;

	if (blocks == 1)
		mrq->stop = NULL;
	else {
		mrq->stop->opcode = MMC_STOP_TRANSMISSION;
		mrq->stop->arg = 0;
		mrq->stop->flags = MMC_RSP_R1B | MMC_CMD_AC;
	}

	mrq->data->blksz = blksz;
	mrq->data->blocks = blocks;
	mrq->data->flags = write ? MMC_DATA_WRITE : MMC_DATA_READ;
	mrq->data->sg = sg;
	mrq->data->sg_len = sg_len;

	mmc_set_data_timeout(mrq->data, test->card);
}

static int mmc_test_busy(struct mmc_command *cmd)
{
	return !(cmd->resp[0] & R1_READY_FOR_DATA) ||
		(R1_CURRENT_STATE(cmd->resp[0]) == R1_STATE_PRG);
}

/*
 * Wait for the card to finish the busy state
 */
static int mmc_test_wait_busy(struct mmcoops_context *test)
{
	int ret, busy;
	struct mmc_command cmd = {0};

	busy = 0;
	do {
		memset(&cmd, 0, sizeof(struct mmc_command));

		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = test->card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		ret = mmc_wait_for_cmd(test->card->host, &cmd, 0);
		if (ret)
			break;

		if (!busy && mmc_test_busy(&cmd)) {
			busy = 1;
			if (test->card->host->caps & MMC_CAP_WAIT_WHILE_BUSY)
				pr_info("%s: Warning: Host did not "
					"wait for busy state to end.\n",
					mmc_hostname(test->card->host));
		}
	} while (mmc_test_busy(&cmd));

	return ret;
}

/*
 * Checks that a normal transfer didn't have any errors
 */
static int mmc_test_check_result(struct mmcoops_context *test,
				 struct mmc_request *mrq)
{
	int ret;

	BUG_ON(!mrq || !mrq->cmd || !mrq->data);

	ret = 0;

	if (!ret && mrq->cmd->error)
		ret = mrq->cmd->error;
	if (!ret && mrq->data->error)
		ret = mrq->data->error;
	if (!ret && mrq->stop && mrq->stop->error)
		ret = mrq->stop->error;
	if (!ret && mrq->data->bytes_xfered !=
		mrq->data->blocks * mrq->data->blksz)
		ret = RESULT_FAIL;

	if (ret == -EINVAL)
		ret = RESULT_UNSUP_HOST;

	return ret;
}

/*
 * Transfer a single sector of kernel addressable data
 */
static int mmc_test_buffer_transfer(struct mmcoops_context *test,
	u8 *buffer, unsigned addr, unsigned blksz, int write)
{
	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};

	struct scatterlist sg;

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;

	sg_init_one(&sg, buffer, blksz);

	mmc_test_prepare_mrq(test, &mrq, &sg, 1, addr, 1, blksz, write);

	mmc_wait_for_req(test->card->host, &mrq);

	if (cmd.error)
		return cmd.error;
	if (data.error)
		return data.error;

	return mmc_test_wait_busy(test);
}

/*
 * Tests a basic transfer with certain parameters
 */
static int mmc_test_simple_transfer(struct mmcoops_context *test,
	struct scatterlist *sg, unsigned sg_len, unsigned dev_addr,
	unsigned blocks, unsigned blksz, int write)
{
	struct mmc_request mrq = {0};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;

	mmc_test_prepare_mrq(test, &mrq, sg, sg_len, dev_addr,
		blocks, blksz, write);

	mmc_wait_for_req(test->card->host, &mrq);

	mmc_test_wait_busy(test);

	return mmc_test_check_result(test, &mrq);
}

/*
 * Does a complete transfer test where data is also validated
 *
 * Note: mmc_test_prepare() must have been done before this call
 */
static int mmc_test_transfer(struct mmcoops_context *test,
	struct scatterlist *sg, unsigned sg_len, unsigned dev_addr,
	unsigned blocks, unsigned blksz, int write)
{
	int ret, i;

	ret = mmc_test_set_blksize(test, blksz);
	if (ret)
		return ret;

	ret = mmc_test_simple_transfer(test, sg, sg_len, dev_addr,
		blocks, blksz, write);
	if (ret)
		return ret;

	if (write) {
		int sectors;

		ret = mmc_test_set_blksize(test, 512);
		if (ret)
			return ret;

		sectors = (blocks * blksz + 511) / 512;
		if ((sectors * 512) == (blocks * blksz))
			sectors++;

		if ((sectors * 512) > BUFFER_SIZE)
			return -EINVAL;

		memset(test->buffer, 0, sectors * 512);

		for (i = 0;i < sectors;i++) {
			ret = mmc_test_buffer_transfer(test,
				test->buffer + i * 512,
				dev_addr + i, 512, 0);
			if (ret)
				return ret;
		}

		for (i = 0;i < blocks * blksz;i++) {
			if (test->buffer[i] != (u8)i)
				return RESULT_FAIL;
		}

		for (;i < sectors * 512;i++) {
			if (test->buffer[i] != 0xDF)
				return RESULT_FAIL;
		}
	}

	return 0;
}

static void mmcoops_do_dump(struct kmsg_dumper *dumper,
		enum kmsg_dump_reason reason)
{
	struct mmcoops_context *cxt = container_of(dumper,
			struct mmcoops_context, dump);
	struct mmc_card *card = cxt->card;
	struct scatterlist sg;

	if (!card)
		return;

	/* Only dump oopses if dump_oops is set */
	if (reason == KMSG_DUMP_OOPS && !dump_oops)
		return;

	kmsg_dump_get_buffer(dumper, true, cxt->buffer, cxt->size_bytes, NULL);

	sg_init_one(&sg, cxt->buffer, cxt->size_bytes);

	mmc_claim_host(card->host);

	mmc_test_transfer(cxt, &sg, 1 , cxt->start_offset_blk,
			  cxt->size_bytes / 512, 512, 1);

	mmc_release_host(card->host);
}

int  mmc_oops_card_set(struct mmc_card *card)
{
	struct mmcoops_context *cxt = &oops_cxt;

	if (!mmc_card_mmc(card) && !mmc_card_sd(card))
		return -ENODEV;

	cxt->card = card;
	pr_info("%s: %s\n", mmc_hostname(card->host), __func__);

	return 0;
}
EXPORT_SYMBOL(mmc_oops_card_set);

static int mmc_oops_probe(struct mmc_card *card)
{
	int ret = 0;

	ret = mmc_oops_card_set(card);
	if (ret)
		return ret;

	return 0;
}

static void mmc_oops_remove(struct mmc_card *card)
{
	mmc_release_host(card->host);
}

/*
 * You can always switch between mmc_test and mmc_block by
 * unbinding / binding e.g.
 *
 *
 * # ls -al /sys/bus/mmc/drivers/mmcblk
 * drwxr-xr-x    2 root     0               0 Jan  1 00:00 .
 * drwxr-xr-x    4 root     0               0 Jan  1 00:00 ..
 * --w-------    1 root     0            4096 Jan  1 00:01 bind
 *  lrwxrwxrwx    1 root     0               0 Jan  1 00:01
 *			mmc0:0001 -> ../../../../class/mmc_host/mmc0/mmc0:0001
 *  --w-------    1 root     0            4096 Jan  1 00:01 uevent
 *  --w-------    1 root     0            4096 Jan  1 00:01 unbind
 *
 *  # echo mmc0:0001 > /sys/bus/mmc/drivers/mmcblk/unbind
 *
 *  # echo mmc0:0001 > /sys/bus/mmc/drivers/mmc_oops/bind
 *  [   48.490814] mmc0: mmc_oops_card_set
 */
static struct mmc_driver mmc_driver = {
	.drv		= {
		.name		= "mmc_oops",
	},
	.probe		= mmc_oops_probe,
	.remove		= mmc_oops_remove,
};

/* Parsing dt node */
static int mmcoops_parse_dt(struct mmcoops_context *cxt)
{
	struct device_node *np = cxt->dev->of_node;
	int ret;

	ret = of_property_read_u32(np, "start-offset-blk",
				   &cxt->start_offset_blk);
	if (ret) {
		dev_err(cxt->dev, "failed to parse 'start-offset-blk'\n");
		return ret;
	}

	ret = of_property_read_u32(np, "size-bytes", &cxt->size_bytes);
	if (ret) {
		dev_err(cxt->dev, "failed to parse 'size-bytes'\n");
		return ret;
	}

	return 0;
}

static int __init mmcoops_probe(struct platform_device *pdev)
{
	struct mmcoops_context *cxt = &oops_cxt;
	int err = -EINVAL;

	err = mmc_register_driver(&mmc_driver);
	if (err)
		return err;

	cxt->card = NULL;
	cxt->dev = &pdev->dev;

	err = mmcoops_parse_dt(cxt);
	if (err) {
		pr_err("mmcoops: parsing mmcoops property failed");
		return err;
	}

	cxt->buffer = kzalloc(cxt->size_bytes, GFP_KERNEL);
	if (!cxt->buffer)
		goto kmalloc_failed;

	cxt->dump.dump = mmcoops_do_dump;

	err = kmsg_dump_register(&cxt->dump);
	if (err) {
		pr_err("mmcoops: registering kmsg dumper failed");
		goto kmsg_dump_register_failed;
	}

	pr_info("mmcoops is probed\n");
	return err;

kmsg_dump_register_failed:
	kfree(cxt->buffer);
kmalloc_failed:
	mmc_unregister_driver(&mmc_driver);

	return err;
}

static int mmcoops_remove(struct platform_device *pdev)
{
	struct mmcoops_context *cxt = &oops_cxt;

	if (kmsg_dump_unregister(&cxt->dump) < 0)
		pr_warn("mmcoops: colud not unregister kmsg dumper");
	kfree(cxt->buffer);
	mmc_unregister_driver(&mmc_driver);

	return 0;
}

static const struct of_device_id mmcoops_match[] = {
	{ .compatible = "mmcoops", },
};

static struct platform_driver mmcoops_driver = {
	.remove			= mmcoops_remove,
	.driver			= {
		.name		= "mmcoops",
		.of_match_table	= mmcoops_match,
	},
};

static int __init mmcoops_init(void)
{
	return platform_driver_probe(&mmcoops_driver, mmcoops_probe);
}

static void __exit mmcoops_exit(void)
{
	platform_driver_unregister(&mmcoops_driver);
}

module_init(mmcoops_init);
module_exit(mmcoops_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jaehoon Chung <jh80.chung@samsung.com>");
MODULE_DESCRIPTION("MMC Oops/Panic Looger");
