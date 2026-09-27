GCOV_PROFILE 			= ${CONFIG_UNISOC_SIPA_V3_GCOV}

obj-$(CONFIG_UNISOC_SIPA_V3) += sipa-core.o

sipa-core-y += sipa_core.o sipa_hal.o sipa_nic.o sipa_skb_recv.o \
			   sipa_skb_send.o sipa_rm_res.o sipa_rm_dep_graph.o \
			   sipa_rm.o sipa_rm_peers_list.o sipa_usb_cons.o \
			   sipa_debugfs.o sipa_phy_r3p0/sipa_cmn_fifo_phy.o \
			   sipa_phy_r3p0/sipa_glb_phy.o sipa_dummy.o \
			   sipa_test.o

# sipa_pam_connect() and sipa_ext_open_pcie() hand the peer's 3-argument notify callbacks to the HAL as its
# 4-argument type: the extra argument is ignored on arm64 (and would trip kCFI, which this kernel does not use)
CFLAGS_sipa_core.o += -Wno-cast-function-type

obj-$(CONFIG_UNISOC_SIPA_ETH) += sipa_eth.o
obj-$(CONFIG_UNISOC_SIPA_USB) += sipa_usb.o
