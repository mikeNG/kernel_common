vfio-platform-calxedaxgmac-y := vfio_platform_calxedaxgmac.o
vfio-platform-amdxgbe-y := vfio_platform_amdxgbe.o
vfio-platform-qcomhidma-y := vfio_platform_qcomhidma.o

ccflags-y += -Idrivers/vfio/platform

obj-$(CONFIG_VFIO_PLATFORM_CALXEDAXGMAC_RESET) += vfio-platform-calxedaxgmac.o
obj-$(CONFIG_VFIO_PLATFORM_QCOMHIDMA_RESET) += vfio-platform-qcomhidma.o
obj-$(CONFIG_VFIO_PLATFORM_AMDXGBE_RESET) += vfio-platform-amdxgbe.o
