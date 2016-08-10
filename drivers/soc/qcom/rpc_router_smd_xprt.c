/* Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * RPCROUTER SMD XPRT module.
 */

#include <linux/module.h>
#include <soc/qcom/smd.h>
#include <soc/qcom/smsm.h>
#include "smd_rpcrouter.h"

struct rpcrouter_smd_xprt {
	struct rpcrouter_xprt xprt;
	smd_channel_t *channel;
};

static struct rpcrouter_smd_xprt smd_remote_xprt;

static int rpcrouter_smd_remote_read_avail(void)
{
	return smd_read_avail(smd_remote_xprt.channel);
}

static int rpcrouter_smd_remote_read(void *data, uint32_t len)
{
	return smd_read(smd_remote_xprt.channel, data, len);
}

static int rpcrouter_smd_remote_write_avail(void)
{
	return smd_write_avail(smd_remote_xprt.channel);
}

static int rpcrouter_smd_remote_write(void *data, uint32_t len, uint32_t type)
{
	return smd_write(smd_remote_xprt.channel, data, len);
}

static int rpcrouter_smd_remote_close(void)
{
	smsm_change_state(SMSM_APPS_STATE, SMSM_RPCINIT, 0);
	return smd_close(smd_remote_xprt.channel);
}

static void rpcrouter_smd_remote_notify(void *dev, unsigned event)
{
	switch (event) {
	case SMD_EVENT_DATA:
		msm_rpcrouter_xprt_notify(&smd_remote_xprt.xprt,
					  RPCROUTER_XPRT_EVENT_DATA);
		break;

	case SMD_EVENT_OPEN:
		pr_info("%s: smd opened 0x%p\n", __func__, dev);

		msm_rpcrouter_xprt_notify(&smd_remote_xprt.xprt,
					  RPCROUTER_XPRT_EVENT_OPEN);
		break;

	case SMD_EVENT_CLOSE:
		pr_info("%s: smd closed 0x%p\n", __func__, dev);

		msm_rpcrouter_xprt_notify(&smd_remote_xprt.xprt,
					  RPCROUTER_XPRT_EVENT_CLOSE);
		break;
	}
}

static int rpcrouter_smd_remote_probe(struct platform_device *pdev)
{
	int rc;

	smd_remote_xprt.xprt.name = "rpcrouter_smd_xprt";
	smd_remote_xprt.xprt.read_avail = rpcrouter_smd_remote_read_avail;
	smd_remote_xprt.xprt.read = rpcrouter_smd_remote_read;
	smd_remote_xprt.xprt.write_avail = rpcrouter_smd_remote_write_avail;
	smd_remote_xprt.xprt.write = rpcrouter_smd_remote_write;
	smd_remote_xprt.xprt.close = rpcrouter_smd_remote_close;
	smd_remote_xprt.xprt.priv = NULL;

	/* Open up SMD channel */
	rc = smd_named_open_on_edge("RPCCALL", SMD_APPS_MODEM,
				    &smd_remote_xprt.channel, NULL,
				    rpcrouter_smd_remote_notify);
	if (rc < 0)
		return rc;

	smd_disable_read_intr(smd_remote_xprt.channel);

	return 0;
}

static struct platform_driver rpcrouter_smd_remote_driver = {
	.probe		= rpcrouter_smd_remote_probe,
	.driver		= {
		.name	= "RPCCALL",
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(rpcrouter_smd_remote_driver);
MODULE_DESCRIPTION("RPC Router SMD XPRT");
MODULE_LICENSE("GPL v2");
