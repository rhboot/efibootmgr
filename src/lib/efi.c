/*
  efi.[ch] - Manipulates EFI variables as exported in /proc/efi/vars

  Copyright (C) 2001,2003 Dell Computer Corporation <Matt_Domsch@dell.com>

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
#include <dirent.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <net/if.h>

typedef unsigned long long u64; /* hack, so we may include kernel's ethtool.h */
typedef __uint32_t u32;         /* ditto */
typedef __uint16_t u16;         /* ditto */
typedef __uint8_t u8;           /* ditto */

#include <linux/ethtool.h>
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

char *
efi_guid_unparse(efi_guid_t *guid, char *out)
{
	sprintf(out, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
		guid->b[3], guid->b[2], guid->b[1], guid->b[0],
		guid->b[5], guid->b[4], guid->b[7], guid->b[6],
		guid->b[8], guid->b[9], guid->b[10], guid->b[11],
		guid->b[12], guid->b[13], guid->b[14], guid->b[15]);
        return out;
}


efi_status_t
read_variable(char *name, efi_variable_t *var)
{
	int newnamesize;
	char *newname;
	int fd;
	size_t readsize;
	if (!name || !var) return EFI_INVALID_PARAMETER;

	newnamesize = strlen(PROC_DIR_EFI_VARS) + strlen(name) + 1;
	newname = malloc(newnamesize);
	if (!newname) return EFI_OUT_OF_RESOURCES;
	sprintf(newname, "%s%s", PROC_DIR_EFI_VARS,name);
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

static efi_status_t
write_variable_to_file(efi_variable_t *var)
{
	int fd, byteswritten;
	if (!var || !opts.testfile) return EFI_INVALID_PARAMETER;

	printf("Test mode: Writing to %s\n", opts.testfile);
	fd = creat(opts.testfile, S_IRWXU);
	if (fd == -1) {
		perror("Couldn't write to testfile");
		return EFI_INVALID_PARAMETER;
	}

	byteswritten = write(fd, var, sizeof(*var));
	if (byteswritten == -1) {
		perror("Writing to testfile");

	}
	close(fd);
	return EFI_SUCCESS;
}
/**
 * select_variable_names()
 * @d - dirent to compare against
 *
 * This ignores "." and ".." entries, and selects all others.
 */

static int
select_variable_names(const struct dirent *d)
{
	if (!strcmp(d->d_name, ".") ||
	    !strcmp(d->d_name, ".."))
		return 0;
	return 1;
}

/**
 * find_write_victim()
 * @var - variable to be written
 * @file - name of file to open for writing @var is returned.
 *
 * This ignores "." and ".." entries, and selects all others.
 */
static char *
find_write_victim(efi_variable_t *var, char file[PATH_MAX])
{
	struct dirent **namelist = NULL;
	int i, n, found=0;
	char testname[PATH_MAX], *p;

	memset(testname, 0, sizeof(testname));
	n = scandir(PROC_DIR_EFI_VARS, &namelist,
		    select_variable_names, alphasort);
	if (n < 0) {
		perror("scandir " PROC_DIR_EFI_VARS);
		fprintf(stderr, "You must 'modprobe efivars' first.\n");
		return NULL;
	}

	p = testname;
	efichar_to_char(p, var->VariableName, PATH_MAX);
	p += strlen(p);
	p += sprintf(p, "-");
	efi_guid_unparse(&var->VendorGuid, p);

	for (i=0; i<n; i++) {
		if (namelist[i] &&
		    strncmp(testname, namelist[i]->d_name, sizeof(testname))) {
			found++;
			sprintf(file, "%s%s", PROC_DIR_EFI_VARS,
				namelist[i]->d_name);
			break;
		}
	}

	while (n--) {
		if (namelist[n]) {
			free(namelist[n]);
			namelist[n] = NULL;
		}
	}
	free(namelist);

	if (!found) return NULL;
	return file;
}


efi_status_t
write_variable(efi_variable_t *var)
{
	int fd;
	size_t writesize;
	char buffer[PATH_MAX], name[PATH_MAX], *p = NULL;

	if (!var) return EFI_INVALID_PARAMETER;
	if (opts.testfile) return write_variable_to_file(var);
	memset(buffer, 0, sizeof(buffer));
	memset(name, 0, sizeof(name));

	p = find_write_victim(var, name);
	if (!p) return EFI_INVALID_PARAMETER;

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
make_mac_addr_device_path(void *buffer, char *mac, uint8_t iftype)
{
        int i;
	MAC_ADDR_DEVICE_PATH *p = buffer;
	p->type = 3;
	p->subtype = 11;
	p->length = sizeof(*p);
	printf("\nmac: ");
	for (i=0; i < 14; i++) {
	  printf("%x", mac[i]);
	  p->macaddr[i] = mac[i];
	}
	p->iftype = iftype;
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

/**
 * make_disk_load_option()
 * @disk disk
 *
 * Returns 0 on error, length of load option created on success.
 */
char *make_disk_load_option(char *p, char *disk)
{
    int disk_fd=0;
    char buffer[80];
    char signature[16];
    int rc, edd_version=0;
    uint8_t mbr_type=0, signature_type=0;
    uint64_t start=0, size=0;
    efi_char16_t os_loader_path[40];

    memset(signature, 0, sizeof(signature));

    disk_fd = open(opts.disk, O_RDWR);
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

    if (rc) {
        fprintf(stderr, "Error: no partition information on disk %s.\n"
		"       Cowardly refusing to create a boot option.\n",
		opts.disk);
	return 0;
    }

    p += make_harddrive_device_path (p, opts.part,
				     start, size,
				     signature,
				     mbr_type, signature_type);

    efichar_from_char(os_loader_path, opts.loader, sizeof(os_loader_path));
    p += make_file_path_device_path (p, os_loader_path);
    p += make_end_device_path       (p);

    return(p);
}

/**
 * make_net_load_option()
 * @data - load option returned
 *
 * Returns 0 on error, length of load option created on success.
 */
char *make_net_load_option(char *p, char *iface)
{
    /* copied pretty much verbatim from the ethtool source */
    int fd = 0, err;
    int bus, slot, func;
    struct ifreq ifr;
    struct ethtool_drvinfo drvinfo;

    memset(&ifr, 0, sizeof(ifr));
    strcpy(ifr.ifr_name, iface);
    drvinfo.cmd = ETHTOOL_GDRVINFO;
    ifr.ifr_data = (caddr_t)&drvinfo;
    /* Open control socket */
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("Cannot get control socket");
    }
    err = ioctl(fd, SIOCETHTOOL, &ifr);
    if (err < 0) {
        perror("Cannot get driver information");
    }

    err = sscanf(drvinfo.bus_info, "%2x:%2x.%x", &bus, &slot, &func);
    if (err == 0) {
        perror("Couldn't parse device location string.");
    }

    p += make_acpi_device_path(p, opts.acpi_hid, opts.acpi_uid);
    p += make_pci_device_path(p, (uint8_t)slot, (uint8_t)func);

    err = ioctl(fd, SIOCGIFHWADDR, &ifr);
    if (err < 0) {
        perror("Cannot get hardware address.");
    }

    p += make_mac_addr_device_path(p, ifr.ifr_ifru.ifru_hwaddr.sa_data, 0);
    p += make_end_device_path       (p);

    return(p);
}

/**
 * make_linux_load_option()
 * @data - load option returned
 *
 * Returns 0 on error, length of load option created on success.
 */
static unsigned long
make_linux_load_option(void *data)
{
	EFI_LOAD_OPTION *load_option = data;
	char *p = data, *q;
	efi_char16_t description[64];
	unsigned long datasize=0;

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

	if (opts.iface) {
	      p = (char *)make_net_load_option(p, opts.iface);
	}

	else {
	      p = (char *)make_disk_load_option(p, opts.iface);
	}

	load_option->file_path_list_length = p - q;

	datasize = (uint8_t *)p - (uint8_t *)data;
	return datasize;
}

/*
 * append_extra_args()
 * appends all arguments from argv[] not snarfed by getopt
 * as one long string onto data, up to maxchars.  allow for nulls
 */

static unsigned long
append_extra_args_ascii(void *data, unsigned long maxchars)
{
	char *p = data;
	int i, appended=0;
	unsigned long usedchars=0;
	if (!data) return 0;


	for (i=opts.optind; i < opts.argc && usedchars < maxchars; i++)	{
		p = strncpy(p, opts.argv[i], maxchars-usedchars-1);
		p += strlen(p);
		appended=1;

		usedchars = p - (char *)data;

		/* Put a space between args */
		if (i < (opts.argc-1)) {

			p = strncpy(p, " ", maxchars-usedchars-1);
			p += strlen(p);
			usedchars = p - (char *)data;
		}

	}
	/* Remember the NULL */
	if (appended) return strlen(data) + 1;
	return 0;
}

static unsigned long
append_extra_args_unicode(void *data, unsigned long maxchars)
{
	char *p = data;
	int i, appended=0;
	unsigned long usedchars=0;
	if (!data) return 0;


	for (i=opts.optind; i < opts.argc && usedchars < maxchars; i++)	{
		p += efichar_from_char((efi_char16_t *)p, opts.argv[i],
				       maxchars-usedchars);
		usedchars = efichar_strsize(data) - sizeof(efi_char16_t);
		appended=1;

		/* Put a space between args */
		if (i < (opts.argc-1)) {
			p += efichar_from_char((efi_char16_t *)p, " ",
					       maxchars-usedchars);
			usedchars = efichar_strsize(data) -
				sizeof(efi_char16_t);
		}
	}

	if (appended) return efichar_strsize( (efi_char16_t *)data );
	return 0;
}


static unsigned long
append_extra_args(void *data, unsigned long maxchars)
{
	if (opts.unicode)
	  return append_extra_args_unicode(data, maxchars);
	else
	  return append_extra_args_ascii(data, maxchars);
}



int
make_linux_efi_variable(efi_variable_t *var,
			unsigned int free_number)
{
	efi_guid_t guid = EFI_GLOBAL_VARIABLE;
	char buffer[16];
	unsigned char *optional_data=NULL;
	unsigned long load_option_size = 0, opt_data_size=0;

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

	load_option_size =  make_linux_load_option(var->Data);

	if (!load_option_size) return 0;

	/* Set OptionalData (passed as binary to the called app) */
	optional_data = var->Data + load_option_size;
	opt_data_size = append_extra_args(optional_data,
				  sizeof(var->Data) - load_option_size);
	var->DataSize = load_option_size + opt_data_size;
	return var->DataSize;
}
