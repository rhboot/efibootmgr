/*
  unparse_path.[ch]

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

/* For PRIx64 */
#define __STDC_FORMAT_MACROS

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>

#include "efi.h"
#include "unparse_path.h"
#include "efichar.h"

/* Avoid unaligned access warnings */
#define get(buf, obj) *(typeof(obj) *)memcpy(buf, &obj, sizeof(obj))

ssize_t
unparse_raw(char *buffer, size_t buffer_size, uint8_t *p, uint64_t length)
{
	uint64_t i;
	char a[1];

	ssize_t needed;
	off_t buf_offset = 0;

	for (i=0; i < length; i++) {
		needed = snprintf(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			"%02x", get(a, p[i]));
		if (needed < 0)
			return -1;
		buf_offset += needed;
	}
	return buf_offset;
}

ssize_t
unparse_raw_text(char *buffer, size_t buffer_size, uint8_t *p, uint64_t length)
{
	uint64_t i; unsigned char c;

	ssize_t needed;
	size_t buf_offset = 0;

	for (i=0; i < length; i++) {
		c = p[i];
		if (c < 32 || c > 127) c = '.';
		needed = snprintf(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			"%c", c);
		if (needed < 0)
			return -1;
		buf_offset += needed;
	}
	return buf_offset;
}

static ssize_t
unparse_ipv4_port(char *buffer, size_t buffer_size, uint32_t ipaddr,
		uint16_t port)
{
	unsigned char *ip;
//	ipaddr = nltoh(ipaddr);
//	port = nstoh(port);
	ip = (unsigned char *)&ipaddr;
	return snprintf(buffer, buffer_size, "%hhu.%hhu.%hhu.%hhu:%hu",
		       ip[0], ip[1], ip[2], ip[3],
		       port);
}

static int
unparse_acpi_path(char *buffer, size_t buffer_size, EFI_DEVICE_PATH *path)
{
	ACPI_DEVICE_PATH *acpi = (ACPI_DEVICE_PATH *)path;
	char a[16], b[16];

	switch (path->subtype) {
	case 1:
		return snprintf(buffer, buffer_size, "ACPI(%x,%x)", get(a, acpi->_HID), get(b, acpi->_UID));
		break;
	default:
		return unparse_raw(buffer, buffer_size, (uint8_t *)path, path->length);
		break;
	}
	return 0;
}

static ssize_t
unparse_vendor_path(char *buffer, size_t buffer_size, char *prefix,
	VENDOR_DEVICE_PATH *path)
{
	char *text_guid = NULL;
	unsigned char *q = (uint8_t *)path + 20;
	int rc;

	ssize_t needed;
	off_t buf_offset = 0;

	rc = efi_guid_to_str(&path->vendor_guid, &text_guid);
	if (rc < 0)
		return -1;

	needed = snprintf(buffer, buffer_size, "%s(%s,",
			prefix ? prefix : "Vendor", text_guid);
	free(text_guid);
	if (needed < 0)
		return -1;
	buf_offset += needed;

	needed = unparse_raw(buffer + buf_offset,
		buffer_size == 0 ? 0 : buffer_size - buf_offset,
		q, path->length - 20);
	if (needed < 0)
		return -1;
	buf_offset += needed;

	needed = snprintf(buffer + buf_offset,
		buffer_size == 0 ? 0 : buffer_size - buf_offset,
		")");
	if (needed < 0)
		return -1;
	buf_offset += needed;

	return buf_offset;
}

static ssize_t
unparse_hardware_path(char *buffer, size_t buffer_size, EFI_DEVICE_PATH *path)
{
	PCI_DEVICE_PATH *pci = (PCI_DEVICE_PATH *)path;
	PCCARD_DEVICE_PATH *pccard = (PCCARD_DEVICE_PATH *)path;
	MEMORY_MAPPED_DEVICE_PATH *mm = (MEMORY_MAPPED_DEVICE_PATH *)path;
	CONTROLLER_DEVICE_PATH *ctlr = (CONTROLLER_DEVICE_PATH *)path;
	char a[16], b[16], c[16];

	switch (path->subtype) {
	case 1:
		return snprintf(buffer, buffer_size, "PCI(%x,%x)",
				get(a, pci->device), get(b, pci->function));
	case 2:
		return snprintf(buffer, buffer_size, "PCCARD(%x)",
				get(a, pccard->socket));
	case 3:
		return snprintf(buffer, buffer_size,
				"MM(%x,%" PRIx64 ",%" PRIx64 ")",
				get(a, mm->memory_type),
				get(b, mm->start),
				get(c, mm->end));
	case 4:
		return unparse_vendor_path(buffer, buffer_size, NULL,
				(VENDOR_DEVICE_PATH *)path);
	case 5:
		return snprintf(buffer, buffer_size, "Controller(%x)",
				get(a, ctlr->controller));
	default:
		return unparse_raw(buffer, buffer_size, (uint8_t *)path,
				path->length);
	}
	return 0;
}

static ssize_t
unparse_messaging_path(char *buffer, size_t buffer_size, EFI_DEVICE_PATH *path)
{
	ATAPI_DEVICE_PATH *atapi = (ATAPI_DEVICE_PATH *)path;
	SATA_DEVICE_PATH *sata = (SATA_DEVICE_PATH *)path;
	SCSI_DEVICE_PATH *scsi = (SCSI_DEVICE_PATH *)path;
	FIBRE_CHANNEL_DEVICE_PATH *fc = (FIBRE_CHANNEL_DEVICE_PATH *)path;
	I1394_DEVICE_PATH *i1394 = (I1394_DEVICE_PATH *)path;
	USB_DEVICE_PATH *usb = (USB_DEVICE_PATH *)path;
	MAC_ADDR_DEVICE_PATH *mac = (MAC_ADDR_DEVICE_PATH *)path;
	USB_CLASS_DEVICE_PATH *usbclass = (USB_CLASS_DEVICE_PATH *)path;
	I2O_DEVICE_PATH *i2o = (I2O_DEVICE_PATH *)path;
	IPv4_DEVICE_PATH *ipv4 = (IPv4_DEVICE_PATH *)path;
/* 	IPv6_DEVICE_PATH *ipv6 = (IPv6_DEVICE_PATH *)path; */
	NVME_DEVICE_PATH *nvme = (NVME_DEVICE_PATH *)path;
	char a[16], b[16], c[16], d[16], e[16];

	ssize_t needed;
	off_t buf_offset = 0;

	switch (path->subtype) {
	case 1:
		return snprintf(buffer, buffer_size, "ATAPI(%x,%x,%x)",
				get(a, atapi->primary_secondary),
				get(b, atapi->slave_master),
				get(c, atapi->lun));
	case 2:
		return snprintf(buffer, buffer_size, "SCSI(%x,%x)",
				get(a, scsi->id), get(b, scsi->lun));
	case 3:
		return snprintf(buffer, buffer_size,
				"FC(%" PRIx64 ",%" PRIx64 ")",
				get(a, fc->wwn), get(b, fc->lun));
	case 4:
		return snprintf(buffer, buffer_size, "1394(%" PRIx64 ")",
				get(a, i1394->guid));
	case 5:
		return snprintf(buffer, buffer_size, "USB(%x,%x)",
				get(a, usb->port), get(b, usb->endpoint));
	case 6:
		return snprintf(buffer, buffer_size, "I2O(%x)",
				get(a, i2o->tid));
	case 10:
		return unparse_vendor_path(buffer, buffer_size, "VenMsg",
			(VENDOR_DEVICE_PATH *)path);
	case 11:
		needed = snprintf(buffer, buffer_size, "MAC(");
		if (needed < 0)
			return needed;
		buf_offset += needed;

		needed = snprintf(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			"MAC(");
		if (needed < 0)
			return needed;
		buf_offset += needed;

		needed = unparse_raw(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			mac->macaddr, 6);
		if (needed < 0)
			return needed;
		buf_offset += needed;

		needed = snprintf(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			",%hhx)", get(a, mac->iftype));
		if (needed < 0)
			return needed;
		buf_offset += needed;

		return buf_offset;
	case 12:
		needed = snprintf(buffer, buf_offset, "IPv4(");
		if (needed < 0)
			return -1;
		buf_offset += needed;

		needed = unparse_ipv4_port(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			ipv4->local_ip, ipv4->local_port);
		if (needed < 0)
			return -1;
		buf_offset += needed;

		needed = snprintf(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			"<->");
		if (needed < 0)
			return -1;
		buf_offset += needed;

		needed = unparse_ipv4_port(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			ipv4->remote_ip, ipv4->remote_port);
		if (needed < 0)
			return -1;
		buf_offset += needed;

		needed = snprintf(buffer + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			",%hx, %hhx", get(a, ipv4->protocol),
			get(b, ipv4->static_addr));
		if (needed < 0)
			return -1;
		buf_offset += needed;

		return buf_offset;
	case 15:
		return snprintf(buffer, buffer_size,
				"USBClass(%hx,%hx,%hhx,%hhx,%hhx)",
				get(a, usbclass->vendor),
				get(b, usbclass->product),
				get(c, usbclass->class),
				get(d, usbclass->subclass),
				get(e, usbclass->protocol));
	case 18:
		return snprintf(buffer, buffer_size,
				"SATA(%hx,%hx,%hx)",
				get(a, sata->port),
				get(b, sata->port_multiplier),
				get(c, sata->lun));
	case 23:
		return snprintf(buffer, buffer_size,
				"NVME(%x,%" PRIx64 ")",
				get(a, nvme->namespace_id),
				get(b, nvme->ieee_extended_unique_identifier));
	default:
		return unparse_raw(buffer, buffer_size,
				(uint8_t *)path, path->length);
	}
	return 0;
}

static ssize_t
unparse_media_hard_drive_path(char *buffer, size_t buffer_size,
				EFI_DEVICE_PATH *path)
{
	HARDDRIVE_DEVICE_PATH *hd = (HARDDRIVE_DEVICE_PATH *)path;
	char text_uuid[40], *sig=text_uuid;
	char a[16], b[16], c[16];
	int rc = 0;
	char *sig_allocated = NULL;

	switch (hd->signature_type) {
	case 0x00:
		rc = sprintf(sig, "None");
		if (rc < 0)
			return -1;
		break;
	case 0x01:
		rc = sprintf(sig, "%08x", *(uint32_t *)memcpy(a, &hd->signature,
						 sizeof(hd->signature)));
		if (rc < 0)
			return -1;
		break;
	case 0x02: /* GPT */
		rc = efi_guid_to_str((efi_guid_t *)hd->signature,
					&sig_allocated);
		if (rc < 0)
			return rc;
		sig = sig_allocated;
		break;
	default:
		return 0;
	}

	rc = snprintf(buffer, buffer_size, "HD(%x,%" PRIx64 ",%" PRIx64 ",%s)",
		       get(a, hd->part_num),
		       get(b, hd->start),
		       get(c, hd->size),
		       sig);
	if (sig_allocated)
		free(sig_allocated);
	return rc;
}

static ssize_t
unparse_media_path(char *buffer, size_t buffer_size, EFI_DEVICE_PATH *path)
{

	CDROM_DEVICE_PATH *cdrom = (CDROM_DEVICE_PATH *)path;
	MEDIA_PROTOCOL_DEVICE_PATH *media = (MEDIA_PROTOCOL_DEVICE_PATH *)path;
	FILE_PATH_DEVICE_PATH *file = (FILE_PATH_DEVICE_PATH *)path;
	char *text_guid = NULL;
	char file_name[80];
	memset(file_name, 0, sizeof(file_name));
	char a[16], b[16], c[16];
	int rc;

	switch (path->subtype) {
	case 1:
		return unparse_media_hard_drive_path(buffer, buffer_size, path);
	case 2:
		return snprintf(buffer, buffer_size,
				"CD-ROM(%x,%" PRIx64 ",%" PRIx64 ")",
				get(a, cdrom->boot_entry),
				get(b, cdrom->start), get(c, cdrom->size));
	case 3:
		return unparse_vendor_path(buffer, buffer_size, NULL,
				(VENDOR_DEVICE_PATH *)path);
	case 4:
		efichar_to_char(file_name, file->path_name, 80);
		return snprintf(buffer, buffer_size, "File(%s)", file_name);
	case 5:
		rc = efi_guid_to_str(&media->guid, &text_guid);
		if (rc < 0)
			return rc;
		rc = snprintf(buffer, buffer_size, "Media(%s)", text_guid);
		free(text_guid);
		return rc;
	case 6:
		rc = efi_guid_to_str(&media->guid, &text_guid);
		if (rc < 0)
			return rc;
		rc = snprintf(buffer, buffer_size, "FvFile(%s)", text_guid);
		free(text_guid);
		return rc > 0 ? rc + 1 : rc;
	case 7:
		rc = efi_guid_to_str(&media->guid, &text_guid);
		if (rc < 0)
			return rc;
		rc = snprintf(buffer, buffer_size, "FvVol(%s)", text_guid);
		free(text_guid);
		return rc > 0 ? rc + 1 : rc;
	}
	return 0;
}

static ssize_t
unparse_bios_path(char *buffer, size_t buffer_size, EFI_DEVICE_PATH *path)
{
	BIOS_BOOT_SPEC_DEVICE_PATH *bios = (BIOS_BOOT_SPEC_DEVICE_PATH *)path;
	char *p = buffer;
	unsigned char *q = (uint8_t *)path + 8;
	char a[16], b[16];

	ssize_t needed;
	off_t buf_offset = 0;

	needed = snprintf(p + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			"BIOS(%x,%x,",
			get(a, bios->device_type), get(b, bios->status_flag));
	if (needed < 0)
		return -1;
	buf_offset += needed;

	needed = unparse_raw(p + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			q, path->length - 8);
	if (needed < 0)
		return -1;
	buf_offset += needed;

	needed = snprintf(p + buf_offset,
			buffer_size == 0 ? 0 : buffer_size - buf_offset,
			")");
	if (needed < 0)
		return -1;
	buf_offset += needed;

	return buf_offset;
}

ssize_t
unparse_path(char *buffer, size_t buffer_size,
		EFI_DEVICE_PATH *path, uint16_t pathsize)
{
	uint16_t parsed_length = 0;
	char *p = buffer;
	ssize_t needed;
	off_t buf_offset = 0;
	int exit_now = 0;

	while (parsed_length < pathsize && !exit_now) {
		switch (path->type) {
		case 0x01:
			needed = unparse_hardware_path(p + buf_offset,
				buffer_size == 0 ? 0 : buffer_size - buf_offset,
				path);
			if (needed < 0)
				return -1;
			buf_offset += needed;
			break;
		case 0x02:
			needed = unparse_acpi_path(p + buf_offset,
				buffer_size == 0 ? 0 : buffer_size - buf_offset,
				path);
			if (needed < 0)
				return -1;
			buf_offset += needed;
			break;
		case 0x03:
			needed = unparse_messaging_path(p + buf_offset,
				buffer_size == 0 ? 0 : buffer_size - buf_offset,
				path);
			if (needed < 0)
				return -1;
			buf_offset += needed;
			break;
		case 0x04:
			needed = unparse_media_path(p + buf_offset,
				buffer_size == 0 ? 0 : buffer_size - buf_offset,
				path);
			if (needed < 0)
				return -1;
			buf_offset += needed;
			break;
		case 0x05:
			needed = unparse_bios_path(p + buf_offset,
				buffer_size == 0 ? 0 : buffer_size - buf_offset,
				path);
			if (needed < 0)
				return -1;
			buf_offset += needed;
			break;
		case 0x7F:
			exit_now = 1;
			break;
		case 0xFF:
			exit_now = 1;
			break;
		default:
			needed = snprintf(p + buf_offset,
				buffer_size == 0 ? 0 : buffer_size - buf_offset,
				"Unknown(%d,%d,", path->type, path->subtype);
			if (needed < 0)
				return -1;
			buf_offset += needed;

			if (path->length + sizeof (END_DEVICE_PATH)
					> (uint64_t)(pathsize - parsed_length)){
				needed = snprintf(p + buf_offset,
					buffer_size == 0
						? 0
						: buffer_size - buf_offset,
					"invalid size)");
				if (needed < 0)
					return -1;
				buf_offset += needed;
				exit_now = 1;
			} else {
				needed = snprintf(p + buf_offset,
					buffer_size == 0
						? 0
						: buffer_size - buf_offset,
					"%d,", path->length);
				if (needed < 0)
					return -1;
				buf_offset += needed;

				needed = unparse_raw(p + buf_offset,
					buffer_size == 0
						? 0
						: buffer_size - buf_offset,
					(uint8_t *)path +
						offsetof(EFI_DEVICE_PATH, data),
					path->length);
				if (needed < 0)
					return -1;
				buf_offset += needed;

				needed = snprintf(p + buf_offset,
					buffer_size == 0
						? 0
						: buffer_size - buf_offset,
					")");
				if (needed < 0)
					return -1;
				buf_offset += needed;
			}
			break;
		}
//		p += sprintf(p, "\\");
		parsed_length += path->length;
		path = (EFI_DEVICE_PATH *) ((uint8_t *)path + path->length);
	}

	return buf_offset;
}

#ifdef UNPARSE_PATH
static void
usage(void)
{
	printf("Usage: dumppath filename\n");
	printf("\t where filename is a blkXXXX EFI variable from /proc/efi/vars\n");
}

int
main(int argc, char **argv)
{
	int fd = 0;
	ssize_t size;
	efi_variable_t var;

	if (argc == 1) {
		usage();
		exit(-1);
	}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("Failed to open file.");
		exit(-1);
	}
	size = read(fd, &var, sizeof(var));
	if (size == -1 || size < sizeof(var)) {
		perror("Failed to read file.");
		close(fd);
		exit(-1);
	}
	unparse_var(&var);

	return 0;
}
#endif
