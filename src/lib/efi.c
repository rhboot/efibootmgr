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

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include "efi.h"
#include "efichar.h"
#include "scsi_ioctls.h"
#include "disk.h"
#include "efibootmgr.h"

EFI_DEVICE_PATH *
load_option_path(EFI_LOAD_OPTION *option)
{
	char *p = (char *) option;
	return (EFI_DEVICE_PATH *)
		(p + sizeof(uint32_t) /* Attributes */
		 + sizeof(uint16_t)   /* FilePathListLength*/
		 + efichar_strsize(option->description)); /* Description */
}

void
efi_guid_unparse(efi_guid_t *guid, char *out)
{
        sprintf(out, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                guid->data1, guid->data2, guid->data3,
                guid->data4[0], guid->data4[1], guid->data4[2], guid->data4[3],
                guid->data4[4], guid->data4[5], guid->data4[6], guid->data4[7]);
}


efi_status_t
read_variable(char *name, efi_variable_t *var)
{
	int newnamesize;
	char *newname;
	int fd;
	size_t readsize;
	if (!name || !var) return EFI_INVALID_PARAMETER;
	
	newnamesize = strlen(PROC_DIR_EFI) + strlen(name) + 1;
	newname = malloc(newnamesize);
	if (!newname) return EFI_OUT_OF_RESOURCES;
	sprintf(newname, "%s%s", PROC_DIR_EFI,name);
	fd = open(newname, O_RDONLY);
	if (fd == -1) {
		free(newname);
		return EFI_NOT_FOUND;
	}
	readsize = read(fd, var, sizeof(*var));
	if (readsize != sizeof(*var)) {
		free(newname);
		close(fd);
		return EFI_INVALID_PARAMETER;
		
	}
	close(fd);
	free(newname);
	return var->Status;
}

efi_status_t
write_variable(efi_variable_t *var)
{
	int fd;
	size_t writesize;
	char buffer[PATH_MAX];
	char name[]="/proc/efi/vars/Efi-47c7b226-c42a-11d2-8e57-00a0c969723b";
	if (!var) return EFI_INVALID_PARAMETER;
	
	fd = open(name, O_WRONLY);
	if (fd == -1) {
		sprintf(buffer, "write_variable():open(%s)", name);
		perror(buffer);
		return EFI_INVALID_PARAMETER;
	}
	writesize = write(fd, var, sizeof(*var));
	if (writesize != sizeof(*var)) {
#if 0
		sprintf(buffer, "write_variable():write(%s)", name);
		perror(buffer);
		dump_raw_data(var, sizeof(*var));
#endif
		close(fd);
		return EFI_INVALID_PARAMETER;
		
	}
	close(fd);
	return EFI_SUCCESS;
}


static int
get_edd_version()
{
	efi_status_t status;
	efi_variable_t var;
	efi_guid_t guid = BLKX_UNKNOWN_GUID;
	char name[80], text_guid[40];
	ACPI_DEVICE_PATH *path = (ACPI_DEVICE_PATH *)&(var.Data);
	int rc = 0;

	/* Allow global user option override */

	switch (opts.edd_version)
	{
	case 0: /* No EDD information */
		return 0;
		break;
	case 1: /* EDD 1.0 */
		return 1;
		break;
	case 3: /* EDD 3.0 */
		return 3;
		break;
	default:
		break;
	}


	memset(&var, 0, sizeof(efi_variable_t));
	efi_guid_unparse(&guid, text_guid);
	sprintf(name, "blk0-%s", text_guid);
	
	status = read_variable(name, &var);
	if (status != EFI_SUCCESS) {
		return 0;
	}
	if (path->type == 2 && path->subtype == 1) rc = 3;
	else rc = 1;
	return rc;
}




/*
  EFI_DEVICE_PATH, 0x01 (Hardware), 0x04 (Vendor), length 0x0018
  This needs to know what EFI device has the boot device.
*/
static uint16_t
make_edd10_device_path(void *buffer, uint32_t hardware_device)
{
	VENDOR_DEVICE_PATH *hw = buffer;
	efi_guid_t guid = EDD10_HARDWARE_VENDOR_PATH_GUID;
	uint32_t *data = (uint32_t *)hw->data;
	hw->type = 0x01; /* Hardware Device Path */
	hw->subtype = 0x04; /* Vendor */
	hw->length = 24;
	memcpy(&(hw->vendor_guid), &guid, sizeof(guid));
	*data = hardware_device;
	return hw->length;
}



static uint16_t
make_end_device_path(void *buffer)
{
	END_DEVICE_PATH *p = buffer;
	p->type = 0x7F; /* End of Hardware Device Path */
	p->subtype = 0xFF; /* End Entire Device Path */
	p->length = sizeof(*p);
	return p->length;
}


static uint16_t
make_acpi_device_path(void *buffer, uint32_t _HID, uint32_t _UID)
{
	ACPI_DEVICE_PATH *p = buffer;
	p->type = 2;
	p->subtype = 1;
	p->length = sizeof(*p);
	p->_HID = _HID;
	p->_UID = _UID;
	return p->length;
}

static uint16_t
make_pci_device_path(void *buffer, uint8_t device, uint8_t function)
{
	PCI_DEVICE_PATH *p = buffer;
	p->type = 1;
	p->subtype = 1;
	p->length   = sizeof(*p);
	p->device   = device;
	p->function = function;
	return p->length;
}

static uint16_t
make_scsi_device_path(void *buffer, uint16_t id, uint16_t lun)
{
	SCSI_DEVICE_PATH *p = buffer;
	p->type = 3;
	p->subtype = 2;
	p->length   = sizeof(*p);
	p->id       = id;
	p->lun      = lun;
	return p->length;
}

static uint16_t
make_harddrive_device_path(void *buffer, uint32_t num, uint64_t start, uint64_t size,
			   uint8_t *signature,
			   uint8_t mbr_type, uint8_t signature_type)
{
	HARDDRIVE_DEVICE_PATH *p = buffer;
	p->type = 4;
	p->subtype = 1;
	p->length   = sizeof(*p);
	p->part_num = num;
	p->start = start;
	p->size = size;
	if (signature) memcpy(p->signature, signature, 16);
	p->mbr_type = mbr_type;
	p->signature_type = signature_type;
	return p->length;
}

static uint16_t
make_file_path_device_path(void *buffer, efi_char16_t *name)
{
	FILE_PATH_DEVICE_PATH *p = buffer;
	int namelen  = efichar_strlen(name, -1);
	int namesize = efichar_strsize(name);
	p->type      = 4;
	p->subtype   = 4;
	p->length    = 4 + namesize;
	efichar_strncpy(p->path_name,
			name, namelen);
	return p->length;

}



static long
make_edd30_device_path(int fd, void *buffer)
{
	int rc=0;
	unsigned char bus=0, device=0, function=0;
	Scsi_Idlun idlun;
	unsigned char host=0, channel=0, id=0, lun=0;
	char *p = buffer;


	rc = disk_get_pci(fd, &bus, &device, &function);
	if (rc) return 0;

	memset(&idlun, 0, sizeof(idlun));
	rc = get_scsi_idlun(fd, &idlun);
	if (rc) return 0;
	idlun_to_components(&idlun, &host, &channel, &id, &lun);

	
	p += make_acpi_device_path      (p, EISAID_PNP0A03, bus);
	p += make_pci_device_path       (p, device, function);
	p += make_scsi_device_path      (p, id, lun);
	return ((void *)p - buffer);
}


static unsigned long
make_linux_load_option(void *data)
{

	EFI_LOAD_OPTION *load_option = data;
	char buffer[80];
	int disk_fd=0;
	char *p = data, *q;
	efi_char16_t description[40];
	efi_char16_t os_loader_path[40];
	int rc, edd_version=0;
	uint64_t start=0, size=0;
	uint8_t mbr_type=0, signature_type=0;
	char signature[16];
	long datasize=0;

	memset(signature, 0, sizeof(signature));

	/* Write Attributes */
	if (opts.active) load_option->attributes = LOAD_OPTION_ACTIVE;
	else             load_option->attributes = 0;

	p += sizeof(uint32_t);
	/* skip writing file_path_list_length */
	p += sizeof(uint16_t);
	/* Write description.  This is the text that appears on the screen for the load option. */
	memset(description, 0, sizeof(description));
	efichar_from_char(description, opts.label, sizeof(description));
	efichar_strncpy(load_option->description, description, sizeof(description));
	p += efichar_strsize(load_option->description); 

	q = p;



	disk_fd = open(opts.disk, O_RDONLY);
	if (disk_fd == -1) {
		sprintf(buffer, "Could not open disk %s", opts.disk);
		perror(buffer);
		return 0;
	}

	if (opts.edd_version) {
		edd_version = get_edd_version();

		if (edd_version == 3) {
			p += make_edd30_device_path(disk_fd, p);
		}
		else if (edd_version == 1) {
			p += make_edd10_device_path(p, opts.edd10_devicenum);
		}
	}
		
	rc = disk_get_partition_info (disk_fd, opts.part,
				      &start, &size, signature,
				      &mbr_type, &signature_type);
	
	close(disk_fd);
	
	if (rc) return 0;

 	p += make_harddrive_device_path (p, opts.part,
					 start, size,
					 signature,
					 mbr_type, signature_type);

	efichar_from_char(os_loader_path, opts.loader, sizeof(os_loader_path));
	p += make_file_path_device_path (p, os_loader_path);
	p += make_end_device_path       (p);

	load_option->file_path_list_length = p - q;

	datasize = (uint8_t *)p - (uint8_t *)data;
	return datasize;
}

void
make_linux_efi_variable(efi_variable_t *var,
			unsigned int free_number)
{
	efi_guid_t guid = EFI_GLOBAL_VARIABLE;
	char buffer[16];

	memset(buffer,    0, sizeof(buffer));
	
	/* VariableName needs to be BootXXXX */
	sprintf(buffer, "Boot%04x", free_number);
	
	efichar_from_char(var->VariableName, buffer, 1024);

	memcpy(&(var->VendorGuid), &guid, sizeof(guid));
	var->Attributes =
		EFI_VARIABLE_NON_VOLATILE |
		EFI_VARIABLE_BOOTSERVICE_ACCESS |
		EFI_VARIABLE_RUNTIME_ACCESS;

	/* Set Data[] and DataSize */

	var->DataSize =  make_linux_load_option(var->Data);
	return;
}
