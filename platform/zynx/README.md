# Zynx platform

32-bit ARM SoC support for Zynx. Named `zynx` so it is not confused with the
existing Xilinx `zynq` platform.

There is no hardware board yet. Bring-up uses QEMU's ARM `virt` machine via
the `zynx-qemu` target until a real memory map and peripherals exist. When
that board is ready, add `target/zynx/` (and a `project/zynx-test.mk`) and
keep this platform as the shared SoC layer.

## Build and run (QEMU)

```
make zynx-qemu-test
scripts/do-qemuarm -p zynx-qemu-test
```

Do not pass `-6`; that selects aarch64. Zynx is ARM32 only (`cortex-a15` on
`virt`, `highmem=off`).
