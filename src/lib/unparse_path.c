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

#include <stdio.h>
#include <stdint.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <netinet/in.h>

#include "efi.h"
#include "unparse_path.h"
#include "efichar.h"




void
dump_raw_data(void *data, uint64_t length)
{
	char buffer1[80], buffer2[80], *b1, *b2, c;
	unsigned char *p = data;
	unsigned long column=0;
	uint64_t length_printed = 0;
	const char maxcolumn = 16;
	while (length_printed < length) {
		b1 = buffer1;
		b2 = buffer2;
		for (column = 0;
		     column < maxcolumn && length_printed < length; 
		     column ++) {
			b1 += sprintf(b1, "%02x ",(unsigned int) *p);
			if (*p < 32 || *p > 126) c = '.';
			else c = *p;
			b2 += sprintf(b2, "%c", c);
			p++;
			length_printed++;
		}
		/* pad out the line */
		for (; column < maxcolumn; column++)
		{
			b1 += sprintf(b1, "   ");
			b2 += sprintf(b2, " ");
		}

		printf("%s\t%s\n", buffer1, buffer2);
	}
}



static int
unparse_raw(char *buffer, uint8_t *p, uint64_t length)
{
	uint64_t i; unsigned char c;
	char *q = buffer;
	for (i=0; i<length; i++) {
		c = p[i];
		//if (c < 32 || c > 127) c = '.';
		buffer += sprintf(buffer, "%02x", c);
	}
	return buffer - q; 
}

static int
unparse_ipv4_port(char *buffer, uint32_t ipaddr, uint16_t port)
{
	unsigned char *ip;
//	ipaddr = nltoh(ipaddr);
//	port = nstoh(port);
	ip = (unsigned char *)&ipaddr;
	return sprintf(buffer, "%hhu.%hhu.%hhu.%hhu:%hu",
		       ip[0], ip[1], ip[2], ip[3], 
		       port);
}


static int
unparse_acpi_path(char *buffer, EFI_DEVICE_PATH *path)
{
	ACPI_DEVICE_PATH *acpi = (ACPI_DEVICE_PATH *)path;

	switch (path->subtype) {
	case 1:
		return sprintf(buffer, "ACPI(%x,%x)", acpi->_HID, acpi->_UID);
		break;
	default:
		return unparse_raw(buffer, (uint8_t *)path, path->length);
		break;
	}
	return 0;
}

static int
unparse_vendor_path(char *buffer, VENDOR_DEVICE_PATH *path)
{
	char text_guid[40], *p = buffer, *q = (uint8_t *)path + 20;
	efi_guid_unparse(&path->vendor_guid, text_guid);
	p += sprintf(p, "Vendor(%s,", text_guid);
	p += unparse_raw(p, q, path->length - 20);
	p += sprintf(p, ")");
	return p - buffer;
}

static int
unparse_hardware_path(char *buffer, EFI_DEVICE_PATH *path)
{
	PCI_DEVICE_PATH *pci = (PCI_DEVICE_PATH *)path;
	PCCARD_DEVICE_PATH *pccard = (PCCARD_DEVICE_PATH *)path;
	MEMORY_MAPPED_DEVICE_PATH *mm = (MEMORY_MAPPED_DEVICE_PATH *)path;
	CONTROLLER_DEVICE_PATH *ctlr = (CONTROLLER_DEVICE_PATH *)path;

	switch (path->subtype) {
	case 1:
		return sprintf(buffer, "PCI(%x,%x)",
			       pci->function, pci->device);
		break;
	case 2:
		return sprintf(buffer, "PCCARD(%x)", pccard->socket);
		break;
	case 3:
		return sprintf(buffer, "MM(%x,%llx,%llx)",
			       mm->memory_type,
			       mm->start, mm->end);
		break;
	case 4:
		return unparse_vendor_path(buffer, (VENDOR_DEVICE_PATH *)path);
		break;

	case 5:
		return sprintf(buffer, "Controller(%x)", ctlr->controller);
		break;

	default:
		return unparse_raw(buffer, (uint8_t *)path, path->length);
	}
	return 0;
}


static int
unparse_messaging_path(char *buffer, EFI_DEVICE_PATH *path)
{
	ATAPI_DEVICE_PATH *atapi = (ATAPI_DEVICE_PATH *)path;
	SCSI_DEVICE_PATH *scsi = (SCSI_DEVICE_PATH *)path;
	FIBRE_CHANNEL_DEVICE_PATH *fc = (FIBRE_CHANNEL_DEVICE_PATH *)path;
	I1394_DEVICE_PATH *i1394 = (I1394_DEVICE_PATH *)path;
	USB_DEVICE_PATH *usb = (USB_DEVICE_PATH *)path;
	MAC_ADDR_DEVICE_PATH *mac = (MAC_ADDR_DEVICE_PATH *)path;
	USB_CLASS_DEVICE_PATH *usbclass = (USB_CLASS_DEVICE_PATH *)path;
	I2O_DEVICE_PATH *i2o = (I2O_DEVICE_PATH *)path; 
	IPv4_DEVICE_PATH *ipv4 = (IPv4_DEVICE_PATH *)path;
	IPv6_DEVICE_PATH *ipv6 = (IPv6_DEVICE_PATH *)path;
	char *p = buffer;

	switch (path->subtype) {
	case 1:
		return sprintf(buffer, "ATAPI(%x,%x,%x)",
			       atapi->primary_secondary,
			       atapi->slave_master, atapi->lun);
		break;
	case 2:
		return sprintf(buffer, "SCSI(%x,%x)", scsi->id, scsi->lun);
		break;

	case 3:
		return sprintf(buffer, "FC(%llx,%llx)", fc->wwn, fc->lun);
		break;
	case 4:
		return sprintf(buffer, "1394(%llx)", i1394->guid);
		break;
	case 5:
		return sprintf(buffer, "USB(%x,%x)", usb->port, usb->endpoint);
		break;
	case 6:
		return sprintf(buffer, "I2O(%x)", i2o->tid);
		break;
	case 11:
		p += sprintf(p, "MAC(");
		p += unparse_raw(p, mac->macaddr, 6);
		p += sprintf(p, ",%hhx)", mac->iftype);
		return (int) (p - buffer);
		break;
	case 12:
		p += sprintf(p, "IPv4(");
		p += unparse_ipv4_port(p, ipv4->local_ip, ipv4->local_port);
		p += sprintf(p, "<->");
		p += unparse_ipv4_port(p, ipv4->remote_ip, ipv4->remote_port);
		p += sprintf(p, ",%hx, %hhx", ipv4->protocol, ipv4->static_addr);
		return (int) (p - buffer);
		break;

	case 15:
		return sprintf(buffer, "USBClass(%hx,%hx,%hhx,%hhx,%hhx)",
			       usbclass->vendor, usbclass->product,
			       usbclass->class, usbclass->subclass,
			       usbclass->protocol);
		break;
	default:
		return unparse_raw(buffer, (uint8_t *)path, path->length);
		break;
	}
	return 0;
}

static int
unparse_media_hard_drive_path(char *buffer, EFI_DEVICE_PATH *path)
{
	HARDDRIVE_DEVICE_PATH *hd = (HARDDRIVE_DEVICE_PATH *)path;
	char text_uuid[40], *sig=text_uuid;
	
	switch (hd->signature_type) {
	case 0x00:
		sprintf(sig, "None");
		break;
	case 0x01:
		sprintf(sig, "%08x", *(uint32_t *)hd->signature);
		break;
	case 0x02: /* GPT */
                efi_guid_unparse((efi_guid_t *)hd->signature, sig);
		break;
	default:
		break;
	}

	return sprintf(buffer, "HD(%x,%llx,%llx,%s)",
		       hd->part_num, hd->start, hd->size, sig);
}



static int
unparse_media_path(char *buffer, EFI_DEVICE_PATH *path)
{

	CDROM_DEVICE_PATH *cdrom = (CDROM_DEVICE_PATH *)path;
	MEDIA_PROTOCOL_DEVICE_PATH *media = (MEDIA_PROTOCOL_DEVICE_PATH *)path;
	FILE_PATH_DEVICE_PATH *file = (FILE_PATH_DEVICE_PATH *)path;
	char text_guid[40], *p = buffer;
	char file_name[80];
	memset(file_name, 0, sizeof(file_name));

	switch (path->subtype) {
	case 1:
		return unparse_media_hard_drive_path(buffer, path);
		break;
	case 2:
		return sprintf(buffer, "CD-ROM(%x,%llx,%llx)",
			       cdrom->boot_entry, cdrom->start, cdrom->size);
		break;
	case 3:
		return unparse_vendor_path(buffer, (VENDOR_DEVICE_PATH *)path);
		break;
	case 4:
		efichar_to_char(file_name, file->path_name, 80);
		return sprintf(p, "File(%s)", file_name);
		break;
	case 5:
		efi_guid_unparse(&media->guid, text_guid);
		return sprintf(buffer, "Media(%s)", text_guid);
		break;
	default:
		break;
	}
	return 0;
}

static int
unparse_bios_path(char *buffer, EFI_DEVICE_PATH *path)
{
	BIOS_BOOT_SPEC_DEVICE_PATH *bios = (BIOS_BOOT_SPEC_DEVICE_PATH *)path;
	char *p = buffer, *q = (uint8_t *)path + 8;
	p += sprintf(p, "BIOS(%x,%x,",
		     bios->device_type, bios->status_flag);
	p += unparse_raw(p, q, path->length - 8);
	p += sprintf(p, ")");
	return p - buffer;
}


uint64_t
unparse_path(char *buffer, EFI_DEVICE_PATH *path, uint16_t pathsize)
{
	uint16_t parsed_length = 0;
	char *p = buffer;
	int exit_now = 0;

	while (parsed_length < pathsize && !exit_now) {
		switch (path->type) {
		case 0x01:
			p += unparse_hardware_path(p, path);
			break;
		case 0x02:
			p += unparse_acpi_path(p, path);
			break;
		case 0x03:
			p += unparse_messaging_path(p, path);
			break;
		case 0x04:
			p += unparse_media_path(p, path);
			break;
		case 0x05:
			p += unparse_bios_path(p, path);
			break;
		case 0x7F:
			exit_now = 1;
			break;
		case 0xFF:
			exit_now = 1;
			break;
		default:
			printf("\nwierd path");
			dump_raw_data(path, 4);
			break;
		}
//		p += sprintf(p, "\\");
		parsed_length += path->length;
		path = (EFI_DEVICE_PATH *) ((uint8_t *)path + path->length);
	}

	return p - buffer;
}


#if 0
static void
unparse_var(efi_variable_t *var)
{
	char buffer[1024];
	memset(buffer, 0, sizeof(buffer));

	unparse_path(buffer, (EFI_DEVICE_PATH *)var->Data, var->DataSize);
	printf("%s\n", buffer);
}

static int
compare_hardware_path_pci(EFI_DEVICE_PATH *path,
			  int device, int func)
{
	uint8_t *p = ((void *)path) + OFFSET_OF(EFI_DEVICE_PATH, data);
	uint8_t path_device, path_func;

	switch (path->subtype) {
	case 1:
		/* PCI */
		path_func   = *(uint8_t *)p;
		path_device = *(uint8_t *)(p+1);
		
		return !(path_func == func && path_device == device);
		
		break;
	default:
		break;
	}
	return 1;
}

static int
compare_hardware_path_scsi(EFI_DEVICE_PATH *path, int id, int lun)
{
	uint8_t *p = ((void *)path) + OFFSET_OF(EFI_DEVICE_PATH, data);
	uint16_t path_id, path_lun;

	switch (path->subtype) {
	case 2:
		/* SCSI */
		path_id   = *(uint16_t *)p;
		path_lun = *(uint16_t *)(p+2);
		
		return !(path_id == id && path_lun == lun);
		break;
	default:
		break;
	}
	return 1;
}

static int
compare_hardware_path_acpi(EFI_DEVICE_PATH *path, int bus)
{
	uint8_t *p = ((void *)path) + OFFSET_OF(EFI_DEVICE_PATH, data);
	uint32_t _HID, _UID;

	switch (path->subtype) {
	case 1:
		/* ACPI */
		_HID = *(uint32_t *)p;
		_UID = *(uint32_t *)(p+4);
		
		/* FIXME: Need to convert _HID and _UID to bus number */

		return 0;
		break;
	default:
		break;
	}
	return 1;
}

static int
compare_media_path_harddrive(EFI_DEVICE_PATH *path, uint32_t num,
			     uint64_t start, uint64_t size)
{
	HARDDRIVE_DEVICE_PATH *p = (HARDDRIVE_DEVICE_PATH *)path;
	

	switch (path->subtype) {
	case 1:
		/* Hard Drive */
		return !(p->part_num == num
			 && p->start == start
			 && p->size == size);
		break;
	default:
		break;
	}
	return 1;
}



int
compare_pci_scsi_disk_blk(efi_variable_t *var,
			  int bus, int device, int func,
			  int host, int channel, int id, int lun,
			  uint64_t start, uint64_t size)
{

	EFI_DEVICE_PATH *path = (EFI_DEVICE_PATH *) var->Data;
	uint64_t parsed_length = 0;
	int exit_now = 0;
	int rc = 0;

	while (parsed_length < var->DataSize && !exit_now && !rc) {
		switch (path->type) {
		case 0x01:
			/* Hardware (PCI) */
			rc = compare_hardware_path_pci(path, device, func);
			break;
		case 0x02:
			/* ACPI */
			rc = compare_hardware_path_acpi(path, bus);
			break;
		case 0x03:
                        /* Messaging (SCSI) */
			rc = compare_messaging_path_scsi(path, id, lun);
			break;
		case 0x04:
			/* Media (Hard Drive) */
			rc = compare_media_path_harddrive(path, 0,
							  start, size);
			break;
		case 0x7F:
		case 0xFF:
			exit_now = 1;
			break;
		case 0x05: /* BIOS */
		default:
			break;
		}
		parsed_length += path->length;
		path = var->Data + parsed_length;
	}
	return rc;
}

#endif	












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
