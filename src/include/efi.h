/*
  efi.[ch] - Extensible Firmware Interface definitions

  Copyright (C) 2001, 2003 Dell Computer Corporation <Matt_Domsch@dell.com>

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
 */
#include <stdint.h>
#include <dirent.h>

#include <efivar.h>

/*******************************************************
 * Boot Option Attributes
 *******************************************************/
#define LOAD_OPTION_ACTIVE 0x00000001

/******************************************************
 * GUIDs
 ******************************************************/
#define DEVICE_PATH_PROTOCOL \
EFI_GUID( 0x09576e91, 0x6d3f, 0x11d2, 0x8e39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define EFI_GLOBAL_VARIABLE \
EFI_GUID( 0x8BE4DF61, 0x93CA, 0x11d2, 0xAA0D, 0x00, 0xE0, 0x98, 0x03, 0x2B, 0x8C)
#define EDD10_HARDWARE_VENDOR_PATH_GUID \
EFI_GUID( 0xCF31FAC5, 0xC24E, 0x11d2, 0x85F3, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B)
#define BLKX_UNKNOWN_GUID \
EFI_GUID( 0x47c7b225, 0xc42a, 0x11d2, 0x8e57, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define DIR_UNKNOWN_GUID \
EFI_GUID( 0x47c7b227, 0xc42a, 0x11d2, 0x8e57, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)
#define ESP_UNKNOWN_GUID \
EFI_GUID( 0x47c7b226, 0xc42a, 0x11d2, 0x8e57, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

typedef uint8_t  efi_bool_t;
typedef uint16_t efi_char16_t;		/* UNICODE character */

typedef struct {
	uint8_t  type;
	uint8_t  subtype;
	uint16_t length;
	uint8_t  data[1];
} __attribute__((packed)) EFI_DEVICE_PATH;

typedef struct {
	uint32_t attributes;
	uint16_t file_path_list_length;
	efi_char16_t description[1];
	EFI_DEVICE_PATH _unused_file_path_list[1];
} __attribute__((packed)) EFI_LOAD_OPTION;


typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t _HID;
	uint32_t _UID;
} __attribute__((packed)) ACPI_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	efi_guid_t vendor_guid;
	uint8_t data[1];
} __attribute__((packed)) VENDOR_DEVICE_PATH;

#define EDD10_HARDWARE_VENDOR_PATH_LENGTH 24

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint8_t function;
	uint8_t device;
} __attribute__((packed)) PCI_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint8_t  socket;
} __attribute__((packed)) PCCARD_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t memory_type;
	uint64_t start;
	uint64_t end;
} __attribute__((packed)) MEMORY_MAPPED_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t controller;
} __attribute__((packed)) CONTROLLER_DEVICE_PATH;


typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint16_t id;
	uint16_t lun;
} __attribute__((packed)) SCSI_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint8_t  primary_secondary;
	uint8_t  slave_master;
	uint16_t lun;
} __attribute__((packed)) ATAPI_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t reserved;
	uint64_t wwn;
	uint64_t lun;
} __attribute__((packed)) FIBRE_CHANNEL_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t reserved;
	uint64_t guid;
} __attribute__((packed)) I1394_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint8_t  port;
	uint8_t  endpoint;
} __attribute__((packed)) USB_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint16_t port;
	uint16_t port_multiplier;
	uint16_t lun;
} __attribute__((packed)) SATA_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint16_t vendor;
	uint16_t product;
	uint8_t  class;
	uint8_t  subclass;
	uint8_t  protocol;
} __attribute__((packed)) USB_CLASS_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t tid;
} __attribute__((packed)) I2O_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint8_t macaddr[32];
	uint8_t iftype;
} __attribute__((packed)) MAC_ADDR_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t local_ip;
	uint32_t remote_ip;
	uint16_t local_port;
	uint16_t remote_port;
	uint16_t protocol;
	uint8_t  static_addr;
} __attribute__((packed)) IPv4_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint8_t  local_ip[16];
	uint8_t  remote_ip[16];
	uint16_t local_port;
	uint16_t remote_port;
	uint16_t protocol;
	uint8_t  static_addr;
} __attribute__((packed)) IPv6_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t namespace_id;
	uint64_t ieee_extended_unique_identifier;
} __attribute__((packed)) NVME_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t reserved;
	uint64_t node_guid;
	uint64_t ioc_guid;
	uint64_t id;
} __attribute__((packed)) INFINIBAND_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t reserved;
	uint64_t baud_rate;
	uint8_t  data_bits;
	uint8_t  parity;
	uint8_t  stop_bits;
} __attribute__((packed)) UART_DEVICE_PATH;


typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t part_num;
	uint64_t start;
	uint64_t size;
	uint8_t  signature[16];
	uint8_t  mbr_type;
	uint8_t  signature_type;
#ifdef __ia64
	uint8_t  padding[6]; /* Emperically needed */
#endif
} __attribute__((packed)) HARDDRIVE_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint32_t boot_entry;
	uint64_t start;
	uint64_t size;
} __attribute__((packed)) CDROM_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	efi_char16_t path_name[1];
} __attribute__((packed)) FILE_PATH_DEVICE_PATH;


typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	efi_guid_t guid;
} __attribute__((packed)) MEDIA_PROTOCOL_DEVICE_PATH;

typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint16_t length;
	uint16_t device_type;
	uint16_t status_flag;
	uint8_t  description[1];
} __attribute__((packed)) BIOS_BOOT_SPEC_DEVICE_PATH;

typedef struct {
	uint8_t  type;
	uint8_t  subtype;
	uint16_t length;
} __attribute__((packed)) END_DEVICE_PATH;


/* Used for ACPI _HID */
#define EISAID_PNP0A03 0xa0341d0

/* Exported functions */

extern EFI_DEVICE_PATH *load_option_path(EFI_LOAD_OPTION *option);

extern int read_boot_var_names(char ***namelist);
extern ssize_t make_linux_load_option(uint8_t **data, size_t *data_size);
extern int append_extra_args(uint8_t **data, size_t *data_size);


#endif /* EFI_H */
