/*
 * Copyright (C) 2007 Advanced Micro Devices, Inc.
 * Author: Leo Duran <leo.duran@amd.com>
 * Author: Wei Wang <wei.wang2@amd.com> - adapted to xen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#ifndef _ASM_X86_64_AMD_IOMMU_DEFS_H
#define _ASM_X86_64_AMD_IOMMU_DEFS_H

/* IOMMU Command Buffer entries: in power of 2 increments, minimum of 256 */
#define IOMMU_CMD_BUFFER_DEFAULT_ENTRIES	512

/* IOMMU Event Log entries: in power of 2 increments, minimum of 256 */
#define IOMMU_EVENT_LOG_DEFAULT_ENTRIES     512

#define PTE_PER_TABLE_SHIFT		9
#define PTE_PER_TABLE_SIZE		(1 << PTE_PER_TABLE_SHIFT)
#define PTE_PER_TABLE_MASK		(~(PTE_PER_TABLE_SIZE - 1))
#define PTE_PER_TABLE_ALIGN(entries) 	\
	(((entries) + PTE_PER_TABLE_SIZE - 1) & PTE_PER_TABLE_MASK)
#define PTE_PER_TABLE_ALLOC(entries)	\
	PAGE_SIZE * (PTE_PER_TABLE_ALIGN(entries) >> PTE_PER_TABLE_SHIFT)

#define PCI_MIN_CAP_OFFSET	0x40
#define PCI_MAX_CAP_BLOCKS	48
#define PCI_CAP_PTR_MASK	0xFC

/* IOMMU Capability */
#define PCI_CAP_ID_MASK		0x000000FF
#define PCI_CAP_ID_SHIFT	0
#define PCI_CAP_NEXT_PTR_MASK	0x0000FF00
#define PCI_CAP_NEXT_PTR_SHIFT	8
#define PCI_CAP_TYPE_MASK	0x00070000
#define PCI_CAP_TYPE_SHIFT	16
#define PCI_CAP_REV_MASK	0x00F80000
#define PCI_CAP_REV_SHIFT	19
#define PCI_CAP_IOTLB_MASK	0x01000000
#define PCI_CAP_IOTLB_SHIFT	24
#define PCI_CAP_HT_TUNNEL_MASK	0x02000000
#define PCI_CAP_HT_TUNNEL_SHIFT	25
#define PCI_CAP_NP_CACHE_MASK	0x04000000
#define PCI_CAP_NP_CACHE_SHIFT	26
#define PCI_CAP_RESET_MASK	0x80000000
#define PCI_CAP_RESET_SHIFT	31

#define PCI_CAP_TYPE_IOMMU		0x3

#define PCI_CAP_MMIO_BAR_LOW_OFFSET	0x04
#define PCI_CAP_MMIO_BAR_HIGH_OFFSET	0x08
#define PCI_CAP_MMIO_BAR_LOW_MASK	0xFFFFC000
#define IOMMU_MMIO_REGION_LENGTH	0x4000

#define PCI_CAP_RANGE_OFFSET		0x0C
#define PCI_CAP_BUS_NUMBER_MASK		0x0000FF00
#define PCI_CAP_BUS_NUMBER_SHIFT	8
#define PCI_CAP_FIRST_DEVICE_MASK	0x00FF0000
#define PCI_CAP_FIRST_DEVICE_SHIFT	16
#define PCI_CAP_LAST_DEVICE_MASK	0xFF000000
#define PCI_CAP_LAST_DEVICE_SHIFT	24

#define PCI_CAP_UNIT_ID_MASK    0x0000001F
#define PCI_CAP_UNIT_ID_SHIFT   0
#define PCI_MISC_INFO_OFFSET    0x10
#define PCI_CAP_MSI_NUMBER_MASK     0x0000001F
#define PCI_CAP_MSI_NUMBER_SHIFT    0

/* Device Table */
#define IOMMU_DEV_TABLE_BASE_LOW_OFFSET		0x00
#define IOMMU_DEV_TABLE_BASE_HIGH_OFFSET	0x04
#define IOMMU_DEV_TABLE_BASE_LOW_MASK		0xFFFFF000
#define IOMMU_DEV_TABLE_BASE_LOW_SHIFT		12
#define IOMMU_DEV_TABLE_BASE_HIGH_MASK		0x000FFFFF
#define IOMMU_DEV_TABLE_BASE_HIGH_SHIFT		0
#define IOMMU_DEV_TABLE_SIZE_MASK		0x000001FF
#define IOMMU_DEV_TABLE_SIZE_SHIFT		0

#define IOMMU_DEV_TABLE_ENTRIES_PER_BUS		256
#define IOMMU_DEV_TABLE_ENTRY_SIZE		32
#define IOMMU_DEV_TABLE_U32_PER_ENTRY		(IOMMU_DEV_TABLE_ENTRY_SIZE / 4)

#define IOMMU_DEV_TABLE_SYS_MGT_DMA_ABORTED	0x0
#define IOMMU_DEV_TABLE_SYS_MGT_MSG_FORWARDED	0x1
#define IOMMU_DEV_TABLE_SYS_MGT_INT_FORWARDED	0x2
#define IOMMU_DEV_TABLE_SYS_MGT_DMA_FORWARDED	0x3

#define IOMMU_DEV_TABLE_IO_CONTROL_ABORTED	0x0
#define IOMMU_DEV_TABLE_IO_CONTROL_FORWARDED	0x1
#define IOMMU_DEV_TABLE_IO_CONTROL_TRANSLATED	0x2

#define IOMMU_DEV_TABLE_INT_CONTROL_ABORTED	0x0
#define IOMMU_DEV_TABLE_INT_CONTROL_FORWARDED	0x1
#define IOMMU_DEV_TABLE_INT_CONTROL_TRANSLATED	0x2

/* DeviceTable Entry[31:0] */
#define IOMMU_DEV_TABLE_VALID_MASK			0x00000001
#define IOMMU_DEV_TABLE_VALID_SHIFT			0
#define IOMMU_DEV_TABLE_TRANSLATION_VALID_MASK		0x00000002
#define IOMMU_DEV_TABLE_TRANSLATION_VALID_SHIFT		1
#define IOMMU_DEV_TABLE_PAGING_MODE_MASK		0x00000E00
#define IOMMU_DEV_TABLE_PAGING_MODE_SHIFT		9
#define IOMMU_DEV_TABLE_PAGE_TABLE_PTR_LOW_MASK		0xFFFFF000
#define IOMMU_DEV_TABLE_PAGE_TABLE_PTR_LOW_SHIFT	12

/* DeviceTable Entry[63:32] */
#define IOMMU_DEV_TABLE_PAGE_TABLE_PTR_HIGH_MASK	0x000FFFFF
#define IOMMU_DEV_TABLE_PAGE_TABLE_PTR_HIGH_SHIFT	0
#define IOMMU_DEV_TABLE_IO_READ_PERMISSION_MASK		0x20000000
#define IOMMU_DEV_TABLE_IO_READ_PERMISSION_SHIFT	29
#define IOMMU_DEV_TABLE_IO_WRITE_PERMISSION_MASK	0x40000000
#define IOMMU_DEV_TABLE_IO_WRITE_PERMISSION_SHIFT	30

/* DeviceTable Entry[95:64] */
#define IOMMU_DEV_TABLE_DOMAIN_ID_MASK	0x0000FFFF
#define IOMMU_DEV_TABLE_DOMAIN_ID_SHIFT	0

/* DeviceTable Entry[127:96] */
#define IOMMU_DEV_TABLE_IOTLB_SUPPORT_MASK		0x00000001
#define IOMMU_DEV_TABLE_IOTLB_SUPPORT_SHIFT		0
#define IOMMU_DEV_TABLE_SUPRESS_LOGGED_PAGES_MASK	0x00000002
#define IOMMU_DEV_TABLE_SUPRESS_LOGGED_PAGES_SHIFT	1
#define IOMMU_DEV_TABLE_SUPRESS_ALL_PAGES_MASK		0x00000004
#define IOMMU_DEV_TABLE_SUPRESS_ALL_PAGES_SHIFT		2
#define IOMMU_DEV_TABLE_IO_CONTROL_MASK			0x00000018
#define IOMMU_DEV_TABLE_IO_CONTROL_SHIFT		3
#define IOMMU_DEV_TABLE_IOTLB_CACHE_HINT_MASK		0x00000020
#define IOMMU_DEV_TABLE_IOTLB_CACHE_HINT_SHIFT		5
#define IOMMU_DEV_TABLE_SNOOP_DISABLE_MASK		0x00000040
#define IOMMU_DEV_TABLE_SNOOP_DISABLE_SHIFT		6
#define IOMMU_DEV_TABLE_ALLOW_EXCLUSION_MASK		0x00000080
#define IOMMU_DEV_TABLE_ALLOW_EXCLUSION_SHIFT		7
#define IOMMU_DEV_TABLE_SYS_MGT_MSG_ENABLE_MASK		0x00000300
#define IOMMU_DEV_TABLE_SYS_MGT_MSG_ENABLE_SHIFT	8

/* DeviceTable Entry[159:128] */
#define IOMMU_DEV_TABLE_INT_VALID_MASK          0x00000001
#define IOMMU_DEV_TABLE_INT_VALID_SHIFT         0
#define IOMMU_DEV_TABLE_INT_TABLE_LENGTH_MASK       0x0000001E
#define IOMMU_DEV_TABLE_INT_TABLE_LENGTH_SHIFT      1
#define IOMMU_DEV_TABLE_INT_TABLE_IGN_UNMAPPED_MASK      0x0000000020
#define IOMMU_DEV_TABLE_INT_TABLE_IGN_UNMAPPED_SHIFT      5
#define IOMMU_DEV_TABLE_INT_TABLE_PTR_LOW_MASK      0xFFFFFFC0
#define IOMMU_DEV_TABLE_INT_TABLE_PTR_LOW_SHIFT     6

/* DeviceTable Entry[191:160] */
#define IOMMU_DEV_TABLE_INT_TABLE_PTR_HIGH_MASK     0x000FFFFF
#define IOMMU_DEV_TABLE_INT_TABLE_PTR_HIGH_SHIFT    0
#define IOMMU_DEV_TABLE_INIT_PASSTHRU_MASK      0x01000000
#define IOMMU_DEV_TABLE_INIT_PASSTHRU_SHIFT     24
#define IOMMU_DEV_TABLE_EINT_PASSTHRU_MASK      0x02000000
#define IOMMU_DEV_TABLE_EINT_PASSTHRU_SHIFT     25
#define IOMMU_DEV_TABLE_NMI_PASSTHRU_MASK       0x04000000
#define IOMMU_DEV_TABLE_NMI_PASSTHRU_SHIFT      26
#define IOMMU_DEV_TABLE_INT_CONTROL_MASK        0x30000000
#define IOMMU_DEV_TABLE_INT_CONTROL_SHIFT       28
#define IOMMU_DEV_TABLE_LINT0_ENABLE_MASK       0x40000000
#define IOMMU_DEV_TABLE_LINT0_ENABLE_SHIFT      30
#define IOMMU_DEV_TABLE_LINT1_ENABLE_MASK       0x80000000
#define IOMMU_DEV_TABLE_LINT1_ENABLE_SHIFT      31

/* Command Buffer */
#define IOMMU_CMD_BUFFER_BASE_LOW_OFFSET	0x08
#define IOMMU_CMD_BUFFER_BASE_HIGH_OFFSET	0x0C
#define IOMMU_CMD_BUFFER_HEAD_OFFSET		0x2000
#define IOMMU_CMD_BUFFER_TAIL_OFFSET		0x2008
#define IOMMU_CMD_BUFFER_BASE_LOW_MASK		0xFFFFF000
#define IOMMU_CMD_BUFFER_BASE_LOW_SHIFT		12
#define IOMMU_CMD_BUFFER_BASE_HIGH_MASK		0x000FFFFF
#define IOMMU_CMD_BUFFER_BASE_HIGH_SHIFT	0
#define IOMMU_CMD_BUFFER_LENGTH_MASK		0x0F000000
#define IOMMU_CMD_BUFFER_LENGTH_SHIFT		24
#define IOMMU_CMD_BUFFER_HEAD_MASK		0x0007FFF0
#define IOMMU_CMD_BUFFER_HEAD_SHIFT		4
#define IOMMU_CMD_BUFFER_TAIL_MASK		0x0007FFF0
#define IOMMU_CMD_BUFFER_TAIL_SHIFT		4

#define IOMMU_CMD_BUFFER_ENTRY_SIZE			16
#define IOMMU_CMD_BUFFER_POWER_OF2_ENTRIES_PER_PAGE	8
#define IOMMU_CMD_BUFFER_U32_PER_ENTRY 	(IOMMU_CMD_BUFFER_ENTRY_SIZE / 4)

#define IOMMU_CMD_OPCODE_MASK			0xF0000000
#define IOMMU_CMD_OPCODE_SHIFT			28
#define IOMMU_CMD_COMPLETION_WAIT		0x1
#define IOMMU_CMD_INVALIDATE_DEVTAB_ENTRY	0x2
#define IOMMU_CMD_INVALIDATE_IOMMU_PAGES	0x3
#define IOMMU_CMD_INVALIDATE_IOTLB_PAGES	0x4
#define IOMMU_CMD_INVALIDATE_INT_TABLE		0x5

/* COMPLETION_WAIT command */
#define IOMMU_COMP_WAIT_DATA_BUFFER_SIZE	8
#define IOMMU_COMP_WAIT_DATA_BUFFER_ALIGNMENT	8
#define IOMMU_COMP_WAIT_S_FLAG_MASK		0x00000001
#define IOMMU_COMP_WAIT_S_FLAG_SHIFT		0
#define IOMMU_COMP_WAIT_I_FLAG_MASK		0x00000002
#define IOMMU_COMP_WAIT_I_FLAG_SHIFT		1
#define IOMMU_COMP_WAIT_F_FLAG_MASK		0x00000004
#define IOMMU_COMP_WAIT_F_FLAG_SHIFT		2
#define IOMMU_COMP_WAIT_ADDR_LOW_MASK		0xFFFFFFF8
#define IOMMU_COMP_WAIT_ADDR_LOW_SHIFT		3
#define IOMMU_COMP_WAIT_ADDR_HIGH_MASK		0x000FFFFF
#define IOMMU_COMP_WAIT_ADDR_HIGH_SHIFT		0

/* INVALIDATE_IOMMU_PAGES command */
#define IOMMU_INV_IOMMU_PAGES_DOMAIN_ID_MASK	0x0000FFFF
#define IOMMU_INV_IOMMU_PAGES_DOMAIN_ID_SHIFT	0
#define IOMMU_INV_IOMMU_PAGES_S_FLAG_MASK	0x00000001
#define IOMMU_INV_IOMMU_PAGES_S_FLAG_SHIFT	0
#define IOMMU_INV_IOMMU_PAGES_PDE_FLAG_MASK	0x00000002
#define IOMMU_INV_IOMMU_PAGES_PDE_FLAG_SHIFT	1
#define IOMMU_INV_IOMMU_PAGES_ADDR_LOW_MASK	0xFFFFF000
#define IOMMU_INV_IOMMU_PAGES_ADDR_LOW_SHIFT	12
#define IOMMU_INV_IOMMU_PAGES_ADDR_HIGH_MASK	0xFFFFFFFF
#define IOMMU_INV_IOMMU_PAGES_ADDR_HIGH_SHIFT	0

/* INVALIDATE_DEVTAB_ENTRY command */
#define IOMMU_INV_DEVTAB_ENTRY_DEVICE_ID_MASK   0x0000FFFF
#define IOMMU_INV_DEVTAB_ENTRY_DEVICE_ID_SHIFT  0

/* INVALIDATE_INTERRUPT_TABLE command */
#define IOMMU_INV_INT_TABLE_DEVICE_ID_MASK   0x0000FFFF
#define IOMMU_INV_INT_TABLE_DEVICE_ID_SHIFT  0

/* Event Log */
#define IOMMU_EVENT_LOG_BASE_LOW_OFFSET		0x10
#define IOMMU_EVENT_LOG_BASE_HIGH_OFFSET	0x14
#define IOMMU_EVENT_LOG_HEAD_OFFSET		0x2010
#define IOMMU_EVENT_LOG_TAIL_OFFSET		0x2018
#define IOMMU_EVENT_LOG_BASE_LOW_MASK		0xFFFFF000
#define IOMMU_EVENT_LOG_BASE_LOW_SHIFT		12
#define IOMMU_EVENT_LOG_BASE_HIGH_MASK		0x000FFFFF
#define IOMMU_EVENT_LOG_BASE_HIGH_SHIFT		0
#define IOMMU_EVENT_LOG_LENGTH_MASK		0x0F000000
#define IOMMU_EVENT_LOG_LENGTH_SHIFT		24
#define IOMMU_EVENT_LOG_HEAD_MASK		0x0007FFF0
#define IOMMU_EVENT_LOG_HEAD_SHIFT		4
#define IOMMU_EVENT_LOG_TAIL_MASK		0x0007FFF0
#define IOMMU_EVENT_LOG_TAIL_SHIFT		4

#define IOMMU_EVENT_LOG_ENTRY_SIZE 			16
#define IOMMU_EVENT_LOG_POWER_OF2_ENTRIES_PER_PAGE	8
#define IOMMU_EVENT_LOG_U32_PER_ENTRY	(IOMMU_EVENT_LOG_ENTRY_SIZE / 4)

#define IOMMU_EVENT_CODE_MASK			0xF0000000
#define IOMMU_EVENT_CODE_SHIFT			28
#define IOMMU_EVENT_ILLEGAL_DEV_TABLE_ENTRY	0x1
#define IOMMU_EVENT_IO_PAGE_FALT		0x2
#define IOMMU_EVENT_DEV_TABLE_HW_ERROR		0x3
#define IOMMU_EVENT_PAGE_TABLE_HW_ERROR		0x4
#define IOMMU_EVENT_ILLEGAL_COMMAND_ERROR	0x5
#define IOMMU_EVENT_COMMAND_HW_ERROR		0x6
#define IOMMU_EVENT_IOTLB_INV_TIMEOUT		0x7
#define IOMMU_EVENT_INVALID_DEV_REQUEST		0x8

#define IOMMU_EVENT_DOMAIN_ID_MASK           0x0000FFFF
#define IOMMU_EVENT_DOMAIN_ID_SHIFT          0
#define IOMMU_EVENT_DEVICE_ID_MASK           0x0000FFFF
#define IOMMU_EVENT_DEVICE_ID_SHIFT          0

/* Control Register */
#define IOMMU_CONTROL_MMIO_OFFSET			0x18
#define IOMMU_CONTROL_TRANSLATION_ENABLE_MASK		0x00000001
#define IOMMU_CONTROL_TRANSLATION_ENABLE_SHIFT		0
#define IOMMU_CONTROL_HT_TUNNEL_TRANSLATION_MASK	0x00000002
#define IOMMU_CONTROL_HT_TUNNEL_TRANSLATION_SHIFT	1
#define IOMMU_CONTROL_EVENT_LOG_ENABLE_MASK		0x00000004
#define IOMMU_CONTROL_EVENT_LOG_ENABLE_SHIFT		2
#define IOMMU_CONTROL_EVENT_LOG_INT_MASK		0x00000008
#define IOMMU_CONTROL_EVENT_LOG_INT_SHIFT		3
#define IOMMU_CONTROL_COMP_WAIT_INT_MASK		0x00000010
#define IOMMU_CONTROL_COMP_WAIT_INT_SHIFT		4
#define IOMMU_CONTROL_TRANSLATION_CHECK_DISABLE_MASK	0x00000020
#define IOMMU_CONTROL_TRANSLATION_CHECK_DISABLE_SHIFT	5
#define IOMMU_CONTROL_INVALIDATION_TIMEOUT_MASK		0x000000C0
#define IOMMU_CONTROL_INVALIDATION_TIMEOUT_SHIFT	6
#define IOMMU_CONTROL_PASS_POSTED_WRITE_MASK		0x00000100
#define IOMMU_CONTROL_PASS_POSTED_WRITE_SHIFT		8
#define IOMMU_CONTROL_RESP_PASS_POSTED_WRITE_MASK	0x00000200
#define IOMMU_CONTROL_RESP_PASS_POSTED_WRITE_SHIFT	9
#define IOMMU_CONTROL_COHERENT_MASK			0x00000400
#define IOMMU_CONTROL_COHERENT_SHIFT			10
#define IOMMU_CONTROL_ISOCHRONOUS_MASK			0x00000800
#define IOMMU_CONTROL_ISOCHRONOUS_SHIFT			11
#define IOMMU_CONTROL_COMMAND_BUFFER_ENABLE_MASK	0x00001000
#define IOMMU_CONTROL_COMMAND_BUFFER_ENABLE_SHIFT	12
#define IOMMU_CONTROL_RESTART_MASK			0x80000000
#define IOMMU_CONTROL_RESTART_SHIFT			31

/* Exclusion Register */
#define IOMMU_EXCLUSION_BASE_LOW_OFFSET		0x20
#define IOMMU_EXCLUSION_BASE_HIGH_OFFSET	0x24
#define IOMMU_EXCLUSION_LIMIT_LOW_OFFSET	0x28
#define IOMMU_EXCLUSION_LIMIT_HIGH_OFFSET	0x2C
#define IOMMU_EXCLUSION_BASE_LOW_MASK		0xFFFFF000
#define IOMMU_EXCLUSION_BASE_LOW_SHIFT		12
#define IOMMU_EXCLUSION_BASE_HIGH_MASK		0xFFFFFFFF
#define IOMMU_EXCLUSION_BASE_HIGH_SHIFT		0
#define IOMMU_EXCLUSION_RANGE_ENABLE_MASK	0x00000001
#define IOMMU_EXCLUSION_RANGE_ENABLE_SHIFT	0
#define IOMMU_EXCLUSION_ALLOW_ALL_MASK		0x00000002
#define IOMMU_EXCLUSION_ALLOW_ALL_SHIFT		1
#define IOMMU_EXCLUSION_LIMIT_LOW_MASK		0xFFFFF000
#define IOMMU_EXCLUSION_LIMIT_LOW_SHIFT		12
#define IOMMU_EXCLUSION_LIMIT_HIGH_MASK		0xFFFFFFFF
#define IOMMU_EXCLUSION_LIMIT_HIGH_SHIFT	0

/* Status Register*/
#define IOMMU_STATUS_MMIO_OFFSET		0x2020
#define IOMMU_STATUS_EVENT_OVERFLOW_MASK	0x00000001
#define IOMMU_STATUS_EVENT_OVERFLOW_SHIFT	0
#define IOMMU_STATUS_EVENT_LOG_INT_MASK		0x00000002
#define IOMMU_STATUS_EVENT_LOG_INT_SHIFT	1
#define IOMMU_STATUS_COMP_WAIT_INT_MASK		0x00000004
#define IOMMU_STATUS_COMP_WAIT_INT_SHIFT	2
#define IOMMU_STATUS_EVENT_LOG_RUN_MASK		0x00000008
#define IOMMU_STATUS_EVENT_LOG_RUN_SHIFT	3
#define IOMMU_STATUS_CMD_BUFFER_RUN_MASK	0x00000010
#define IOMMU_STATUS_CMD_BUFFER_RUN_SHIFT	4

/* I/O Page Table */
#define IOMMU_PAGE_TABLE_ENTRY_SIZE	8
#define IOMMU_PAGE_TABLE_U32_PER_ENTRY	(IOMMU_PAGE_TABLE_ENTRY_SIZE / 4)
#define IOMMU_PAGE_TABLE_ALIGNMENT	4096

#define IOMMU_PTE_PRESENT_MASK			0x00000001
#define IOMMU_PTE_PRESENT_SHIFT			0
#define IOMMU_PTE_NEXT_LEVEL_MASK		0x00000E00
#define IOMMU_PTE_NEXT_LEVEL_SHIFT		9
#define IOMMU_PTE_ADDR_LOW_MASK			0xFFFFF000
#define IOMMU_PTE_ADDR_LOW_SHIFT		12
#define IOMMU_PTE_ADDR_HIGH_MASK		0x000FFFFF
#define IOMMU_PTE_ADDR_HIGH_SHIFT		0
#define IOMMU_PTE_U_MASK			0x08000000
#define IOMMU_PTE_U_SHIFT			7
#define IOMMU_PTE_FC_MASK			0x10000000
#define IOMMU_PTE_FC_SHIFT			28
#define IOMMU_PTE_IO_READ_PERMISSION_MASK	0x20000000
#define IOMMU_PTE_IO_READ_PERMISSION_SHIFT	29
#define IOMMU_PTE_IO_WRITE_PERMISSION_MASK	0x40000000
#define IOMMU_PTE_IO_WRITE_PERMISSION_SHIFT	30

/* I/O Page Directory */
#define IOMMU_PAGE_DIRECTORY_ENTRY_SIZE		8
#define IOMMU_PAGE_DIRECTORY_ALIGNMENT		4096
#define IOMMU_PDE_PRESENT_MASK			0x00000001
#define IOMMU_PDE_PRESENT_SHIFT			0
#define IOMMU_PDE_NEXT_LEVEL_MASK		0x00000E00
#define IOMMU_PDE_NEXT_LEVEL_SHIFT		9
#define IOMMU_PDE_ADDR_LOW_MASK			0xFFFFF000
#define IOMMU_PDE_ADDR_LOW_SHIFT		12
#define IOMMU_PDE_ADDR_HIGH_MASK		0x000FFFFF
#define IOMMU_PDE_ADDR_HIGH_SHIFT		0
#define IOMMU_PDE_IO_READ_PERMISSION_MASK	0x20000000
#define IOMMU_PDE_IO_READ_PERMISSION_SHIFT	29
#define IOMMU_PDE_IO_WRITE_PERMISSION_MASK	0x40000000
#define IOMMU_PDE_IO_WRITE_PERMISSION_SHIFT	30

/* Paging modes */
#define IOMMU_PAGING_MODE_DISABLED	0x0
#define IOMMU_PAGING_MODE_LEVEL_0	0x0
#define IOMMU_PAGING_MODE_LEVEL_1	0x1
#define IOMMU_PAGING_MODE_LEVEL_2	0x2
#define IOMMU_PAGING_MODE_LEVEL_3	0x3
#define IOMMU_PAGING_MODE_LEVEL_4	0x4
#define IOMMU_PAGING_MODE_LEVEL_5	0x5
#define IOMMU_PAGING_MODE_LEVEL_6	0x6
#define IOMMU_PAGING_MODE_LEVEL_7	0x7

/* Flags */
#define IOMMU_CONTROL_DISABLED	0
#define IOMMU_CONTROL_ENABLED	1

#define MMIO_PAGES_PER_IOMMU        (IOMMU_MMIO_REGION_LENGTH / PAGE_SIZE_4K)
#define IOMMU_PAGES                 (MMIO_PAGES_PER_IOMMU * MAX_AMD_IOMMUS)
#define DEFAULT_DOMAIN_ADDRESS_WIDTH    48
#define MAX_AMD_IOMMUS                  32
#define IOMMU_PAGE_TABLE_LEVEL_3        3
#define IOMMU_PAGE_TABLE_LEVEL_4        4
#define IOMMU_IO_WRITE_ENABLED          1
#define IOMMU_IO_READ_ENABLED           1
#define HACK_BIOS_SETTINGS                  0

/* interrupt remapping table */
#define INT_REMAP_INDEX_DM_MASK         0x1C00
#define INT_REMAP_INDEX_DM_SHIFT        10
#define INT_REMAP_INDEX_VECTOR_MASK     0x3FC
#define INT_REMAP_INDEX_VECTOR_SHIFT    2
#define INT_REMAP_ENTRY_REMAPEN_MASK    0x00000001
#define INT_REMAP_ENTRY_REMAPEN_SHIFT   0
#define INT_REMAP_ENTRY_SUPIOPF_MASK    0x00000002
#define INT_REMAP_ENTRY_SUPIOPF_SHIFT   1
#define INT_REMAP_ENTRY_INTTYPE_MASK    0x0000001C
#define INT_REMAP_ENTRY_INTTYPE_SHIFT   2
#define INT_REMAP_ENTRY_REQEOI_MASK     0x00000020
#define INT_REMAP_ENTRY_REQEOI_SHIFT    5
#define INT_REMAP_ENTRY_DM_MASK         0x00000040
#define INT_REMAP_ENTRY_DM_SHIFT        6
#define INT_REMAP_ENTRY_DEST_MAST       0x0000FF00
#define INT_REMAP_ENTRY_DEST_SHIFT      8
#define INT_REMAP_ENTRY_VECTOR_MASK     0x00FF0000
#define INT_REMAP_ENTRY_VECTOR_SHIFT    16

#endif /* _ASM_X86_64_AMD_IOMMU_DEFS_H */
