/*
 * Copyright (c) 2015 Travis Geiselbrecht
 * Copyright (c) 2026 Zynx
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */
#pragma once

/*
 * Memory map while running on QEMU `virt` (hw/arm/virt.c). The eventual
 * hardware target will replace these constants with the real SoC map.
 */

/* up to 1 GB of ram in the 32-bit aperture */
#define MEMORY_BASE_PHYS     (0x40000000)
#define MEMORY_APERTURE_SIZE (1UL * 1024 * 1024 * 1024)

/* map all of 0-1GB into kernel space in one shot */
#define PERIPHERAL_BASE_PHYS (0)
#define PERIPHERAL_BASE_SIZE (0x40000000UL) // 1GB
#define PERIPHERAL_BASE_VIRT (0xc0000000UL) // -1GB

/* individual peripherals in this mapping */
#define FLASH_BASE_VIRT     (PERIPHERAL_BASE_VIRT + 0)
#define FLASH_SIZE          (0x08000000)
#define CPUPRIV_BASE_VIRT   (PERIPHERAL_BASE_VIRT + 0x08000000)
#define CPUPRIV_BASE_PHYS   (PERIPHERAL_BASE_PHYS + 0x08000000)
#define CPUPRIV_SIZE        (0x00020000)
#define UART_BASE           (PERIPHERAL_BASE_VIRT + 0x09000000)
#define UART_SIZE           (0x00001000)
#define RTC_BASE            (PERIPHERAL_BASE_VIRT + 0x09010000)
#define RTC_SIZE            (0x00001000)
#define FW_CFG_BASE         (PERIPHERAL_BASE_VIRT + 0x09020000)
#define FW_CFG_SIZE         (0x00001000)
#define NUM_VIRTIO_TRANSPORTS 32
#define VIRTIO_BASE         (PERIPHERAL_BASE_VIRT + 0x0a000000)
#define VIRTIO_SIZE         (NUM_VIRTIO_TRANSPORTS * 0x200)

/* interrupts */
#define ARM_GENERIC_TIMER_VIRTUAL_INT 27
#define ARM_GENERIC_TIMER_PHYSICAL_INT 30
#define UART0_INT   (32 + 1)
#define PCIE_INT_BASE (32 + 3)
#define VIRTIO0_INT_BASE (32 + 16)
#define MSI_INT_BASE (32 + 48)

#define MAX_INT 128
