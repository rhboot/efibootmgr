/*
    gpt.[ch]

    Copyright (C) 2000-2001 Dell Computer Corporation <Matt_Domsch@dell.com> 

    EFI GUID Partition Table handling
    Per Intel EFI Specification v1.02
    http://developer.intel.com/technology/efi/efi.htm

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef _GPT_H
#define _GPT_H


#include <linux/types.h>
#include "efi.h"

#define EFI_PMBR_OSTYPE_EFI 0xEF
#define EFI_PMBR_OSTYPE_EFI_GPT 0xEE
#define MSDOS_MBR_SIGNATURE 0xaa55
#define GPT_BLOCK_SIZE 512


#define GPT_HEADER_SIGNATURE 0x5452415020494645
#define GPT_HEADER_REVISION_V1_02 0x00010200
#define GPT_HEADER_REVISION_V1_00 0x00010000
#define GPT_HEADER_REVISION_V0_99 0x00009900


#define UNUSED_ENTRY_GUID    \
    ((efi_guid_t) { 0x00000000, 0x0000, 0x0000, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }})
#define PARTITION_SYSTEM_GUID \
    ((efi_guid_t) { 0xC12A7328, 0xF81F, 0x11d2, { 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B }})
#define LEGACY_MBR_PARTITION_GUID \
    ((efi_guid_t) { 0x024DEE41, 0x33E7, 0x11d3, { 0x9D, 0x69, 0x00, 0x08, 0xC7, 0x81, 0xF3, 0x9F }})
#define PARTITION_MSFT_RESERVED_GUID \
    ((efi_guid_t) { 0xE3C9E316, 0x0B5C, 0x4DB8, { 0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE }})
#define PARTITION_BASIC_DATA_GUID \
    ((efi_guid_t) { 0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 }})
#define PARTITION_RAID_GUID \
    ((efi_guid_t) { 0xa19d880f, 0x05fc, 0x4d3b, { 0xa0, 0x06, 0x74, 0x3f, 0x0f, 0x84, 0x91, 0x1e }})
#define PARTITION_SWAP_GUID \
    ((efi_guid_t) { 0x0657fd6d, 0xa4ab, 0x43c4, { 0x84, 0xe5, 0x09, 0x33, 0xc8, 0x4b, 0x4f, 0x4f }})
#define PARTITION_LVM_GUID \
    ((efi_guid_t) { 0xe6d6d379, 0xf507, 0x44c2, { 0xa2, 0x3c, 0x23, 0x8f, 0x2a, 0x3d, 0xf9, 0x28 }})
#define PARTITION_RESERVED_GUID \
    ((efi_guid_t) { 0x8da63339, 0x0007, 0x60c0, { 0xc4, 0x36, 0x08, 0x3a, 0xc8, 0x23, 0x09, 0x08 }})

typedef struct _GuidPartitionTableHeader_t {
	__u64 Signature;
	__u32 Revision;
	__u32 HeaderSize;
	__u32 HeaderCRC32;
	__u32 Reserved1;
	__u64 MyLBA;
	__u64 AlternateLBA;
	__u64 FirstUsableLBA;
	__u64 LastUsableLBA;
	efi_guid_t DiskGUID;
	__u64 PartitionEntryLBA;
	__u32 NumberOfPartitionEntries;
	__u32 SizeOfPartitionEntry;
	__u32 PartitionEntryArrayCRC32;
	__u8 Reserved2[GPT_BLOCK_SIZE - 92];
} __attribute__ ((packed)) GuidPartitionTableHeader_t;

typedef struct _GuidPartitionEntryAttributes_t {
	__u64 RequiredToFunction:1;
	__u64 Reserved:47;
        __u64 GuidSpecific:16;
} __attribute__ ((packed)) GuidPartitionEntryAttributes_t;

typedef struct _GuidPartitionEntry_t {
	efi_guid_t PartitionTypeGuid;
	efi_guid_t UniquePartitionGuid;
	__u64 StartingLBA;
	__u64 EndingLBA;
	GuidPartitionEntryAttributes_t Attributes;
	efi_char16_t PartitionName[72 / sizeof(efi_char16_t)];
} __attribute__ ((packed)) GuidPartitionEntry_t;


/* 
   These values are only defaults.  The actual on-disk structures
   may define different sizes, so use those unless creating a new GPT disk!
*/

#define GPT_DEFAULT_RESERVED_PARTITION_ENTRY_ARRAY_SIZE 16384
/* 
   Number of actual partition entries should be calculated
   as: 
*/
#define GPT_DEFAULT_RESERVED_PARTITION_ENTRIES \
        (GPT_DEFAULT_RESERVED_PARTITION_ENTRY_ARRAY_SIZE / \
         sizeof(GuidPartitionEntry_t))


typedef struct _PartitionRecord_t {
	__u8 BootIndicator;	/* Not used by EFI firmware. Set to 0x80 to indicate that this
				   is the bootable legacy partition. */
	__u8 StartHead;		/* Start of partition in CHS address, not used by EFI firmware. */
	__u8 StartSector;	/* Start of partition in CHS address, not used by EFI firmware. */
	__u8 StartTrack;	/* Start of partition in CHS address, not used by EFI firmware. */
	__u8 OSType;		/* OS type. A value of 0xEF defines an EFI system partition.
				   Other values are reserved for legacy operating systems, and
				   allocated independently of the EFI specification. */
	__u8 EndHead;		/* End of partition in CHS address, not used by EFI firmware. */
	__u8 EndSector;		/* End of partition in CHS address, not used by EFI firmware. */
	__u8 EndTrack;		/* End of partition in CHS address, not used by EFI firmware. */
	__u32 StartingLBA;	/* Starting LBA address of the partition on the disk. Used by
				   EFI firmware to define the start of the partition. */
	__u32 SizeInLBA;	/* Size of partition in LBA. Used by EFI firmware to determine
				   the size of the partition. */
} __attribute__ ((packed)) PartitionRecord_t;


/* Protected Master Boot Record  & Legacy MBR share same structure */
/* Needs to be packed because the u16s force misalignment. */

typedef struct _LegacyMBR_t {
	__u8 BootCode[440];
	__u32 UniqueMBRSignature;
	__u16 Unknown;
	PartitionRecord_t PartitionRecord[4];
	__u16 Signature;
} __attribute__ ((packed)) LegacyMBR_t;




#define EFI_GPT_PRIMARY_PARTITION_TABLE_LBA 1

/* Functions */
int gpt_disk_get_partition_info (int fd, 
                                 __u32 num,
                                 __u64 *start, __u64 *size,
                                 char *signature,
                                 __u8 *mbr_type, __u8 *signature_type);


#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4 
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
