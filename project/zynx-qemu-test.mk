# Zynx ARM32 bring-up on QEMU virt until real hardware exists.
MODULES += \
	app/shell

include project/virtual/test.mk
include project/virtual/fs.mk
include project/virtual/minip.mk
include project/target/zynx-qemu.mk
