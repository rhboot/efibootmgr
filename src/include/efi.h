/*
  efi.[ch] - Manipulates EFI variables as exported in /proc/efi/vars
 
  Copyright (C) 2001 Dell Computer Corporation <Matt_Domsch@dell.com>
 
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

#ifndef EFI_H
#define EFI_H

/*
 * Extensible Firmware Interface
 * Based on 'Extensible Firmware Interface Specification'
 *      version 1.02, 12 December, 2000
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999 Hewlett-Packard Co.
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2001 Matt Domsch <Matt_Domsch@dell.com>
 */
#include <linux/types.h>

#define EFI_SUCCESS		0
#define EFI_LOAD_ERROR          (1L | (1L << 63))
#define EFI_INVALID_PARAMETER	(2L | (1L << 63))
#define EFI_UNSUPPORTED		(3L | (1L << 63))
#define EFI_BAD_BUFFER_SIZE     (4L | (1L << 63))
#define EFI_BUFFER_TOO_SMALL	(5L | (1L << 63))
#define EFI_NOT_FOUND          (14L | (1L << 63))
#define EFI_OUT_OF_RESOURCES   (15L | (1L << 63))


/*******************************************************
 * Boot Option Attributes
 *******************************************************/
#define LOAD_OPTION_ACTIVE 0x00000001

/******************************************************
 * EFI Variable Attributes
 ******************************************************/
#define EFI_VARIABLE_NON_VOLATILE 0x0000000000000001
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x0000000000000002
#define EFI_VARIABLE_RUNTIME_ACCESS 0x0000000000000004

/******************************************************
 * GUIDs
 ******************************************************/
#define DEVICE_PATH_PROTOCOL \
{ 0x09576e91, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define EFI_GLOBAL_VARIABLE \
{ 0x8BE4DF61, 0x93CA, 0x11d2, {0xAA, 0x0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C}}
#define EDD10_HARDWARE_VENDOR_PATH_GUID \
{ 0xCF31FAC5, 0xC24E, 0x11d2, {0x85, 0xF3, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}}
#define BLKX_UNKNOWN_GUID \
{ 0x47c7b225, 0xc42a, 0x11d2, {0x8e, 0x57, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define DIR_UNKNOWN_GUID \
{ 0x47c7b227, 0xc42a, 0x11d2, {0x8e, 0x57, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}
#define ESP_UNKNOWN_GUID \
{ 0x47c7b226, 0xc42a, 0x11d2, {0x8e, 0x57, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}



typedef __u64 efi_status_t;
typedef __u8  efi_bool_t;
typedef __u16 efi_char16_t;		/* UNICODE character */

typedef struct {
	__u32 data1;
	__u16 data2;
	__u16 data3;
	__u8  data4[8];
} efi_guid_t;

typedef struct _efi_variable_t {
        efi_char16_t  VariableName[1024/sizeof(efi_char16_t)];
        efi_guid_t    VendorGuid;
        __u64         DataSize;
        __u8          Data[1024];
	efi_status_t  Status;
        __u32         Attributes;
} __attribute__((packed)) efi_variable_t;



typedef struct {
	__u8  type;
	__u8  subtype;
	__u16 length;
	__u8  data[1];
} __attribute__((packed)) EFI_DEVICE_PATH;

typedef struct {
	__u32 attributes;
	__u16 file_path_list_length;
	efi_char16_t description[1];
	EFI_DEVICE_PATH _unused_file_path_list[1];
} __attribute__((packed)) EFI_LOAD_OPTION;


typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 _HID;
	__u32 _UID;
} __attribute__((packed)) ACPI_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	efi_guid_t vendor_guid;
	__u8 data[1];
} __attribute__((packed)) VENDOR_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u8 function;
	__u8 device;
} __attribute__((packed)) PCI_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u8  socket;
} __attribute__((packed)) PCCARD_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 memory_type;
	__u64 start;
	__u64 end;
} __attribute__((packed)) MEMORY_MAPPED_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 controller;
} __attribute__((packed)) CONTROLLER_DEVICE_PATH;





typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u16 id;
	__u16 lun;
} __attribute__((packed)) SCSI_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u8  primary_secondary;
	__u8  slave_master;
	__u16 lun;
} __attribute__((packed)) ATAPI_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 reserved;
	__u64 wwn;
	__u64 lun;
} __attribute__((packed)) FIBRE_CHANNEL_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 reserved;
	__u64 guid;
} __attribute__((packed)) I1394_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u8  port;
	__u8  endpoint;
} __attribute__((packed)) USB_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u16 vendor;
	__u16 product;
	__u8  class;
	__u8  subclass;
	__u8  protocol;
} __attribute__((packed)) USB_CLASS_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 tid;
} __attribute__((packed)) I2O_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u8 macaddr[32];
	__u8 iftype;
} __attribute__((packed)) MAC_ADDR_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 local_ip;
	__u32 remote_ip;
	__u16 local_port;
	__u16 remote_port;
	__u16 protocol;
	__u8  static_addr;
} __attribute__((packed)) IPv4_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u8  local_ip[16];
	__u8  remote_ip[16];
	__u16 local_port;
	__u16 remote_port;
	__u16 protocol;
	__u8  static_addr;
} __attribute__((packed)) IPv6_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 reserved;
	__u64 node_guid;
	__u64 ioc_guid;
	__u64 id; 
} __attribute__((packed)) INFINIBAND_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 reserved;
	__u64 baud_rate;
	__u8  data_bits;
	__u8  parity;
	__u8  stop_bits;
} __attribute__((packed)) UART_DEVICE_PATH;


typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 part_num;
	__u64 start;
	__u64 size;
	__u8  signature[16];
	__u8  mbr_type;
	__u8  signature_type;
} __attribute__((packed)) HARDDRIVE_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u32 boot_entry;
	__u64 start;
	__u64 size;
} __attribute__((packed)) CDROM_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	efi_char16_t path_name[1];
} __attribute__((packed)) FILE_PATH_DEVICE_PATH;


typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	efi_guid_t guid;
} __attribute__((packed)) MEDIA_PROTOCOL_DEVICE_PATH;

typedef struct {
	__u8 type;
	__u8 subtype;
	__u16 length;
	__u16 device_type;
	__u16 status_flag;
	__u8  description[1];
} __attribute__((packed)) BIOS_BOOT_SPEC_DEVICE_PATH;

typedef struct {
	__u8  type;
	__u8  subtype;
	__u16 length;
} __attribute__((packed)) END_DEVICE_PATH;

	
/* Used for ACPI _HID */
#define EISAID_PNP0A03 0xa0341d0

#define PROC_DIR_EFI "/proc/efi/vars/"



/* Exported functions */

efi_status_t read_variable(char *name, efi_variable_t *var);
efi_status_t write_variable(efi_variable_t *var);
void char_to_efichar(char *s1, efi_char16_t *s2, size_t s2_len);
void make_linux_efi_variable(efi_variable_t *var,
			     unsigned int free_number);
void efi_guid_unparse(efi_guid_t *guid, char *out);
EFI_DEVICE_PATH *load_option_path(EFI_LOAD_OPTION *option);





#endif /* _ASM_IA64_EFI_H */
