/*
  efivars_proc.[ch] - Manipulates EFI variables as exported in /proc/efi/vars

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

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <efivar.h>
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
#include <linux/types.h>
#include <net/if.h>
#include <pci/pci.h>
#include <asm/types.h>
#include <linux/ethtool.h>
#include "efi.h"
#include "efichar.h"
#include "scsi_ioctls.h"
#include "disk.h"
#include "efibootmgr.h"
#include "list.h"

EFI_DEVICE_PATH *
load_option_path(EFI_LOAD_OPTION *option)
{
	char *p = (char *) option;
	return (EFI_DEVICE_PATH *)
		(p + sizeof(uint32_t) /* Attributes */
		 + sizeof(uint16_t)   /* FilePathListLength*/
		 + efichar_strsize(option->description)); /* Description */
}

static int
select_boot_var_names(const efi_guid_t *guid, const char *name)
{
	efi_guid_t global = EFI_GLOBAL_GUID;
	if (!strncmp(name, "Boot", 4) &&
			isxdigit(name[4]) && isxdigit(name[5]) &&
			isxdigit(name[6]) && isxdigit(name[7]) &&
			!memcmp(guid, &global, sizeof (global)))
		return 1;
	return 0;
}
typedef __typeof__(select_boot_var_names) filter_t;

static int
cmpstringp(const void *p1, const void *p2)
{
	const char *s1 = *(const char **)p1;
	const char *s2 = *(const char **)p2;
	return strcoll(s1, s2);
}

static int
read_var_names(filter_t filter, char ***namelist)
{
	int rc;
	efi_guid_t *guid = NULL;
	char *name = NULL;
	char **newlist = NULL;
	int nentries = 0;
	int i;

	rc = efi_variables_supported();
	if (!rc)
		return -1;

	while ((rc = efi_get_next_variable_name(&guid, &name)) > 0) {
		if (!filter(guid, name))
			continue;

		char *aname = strdup(name);
		if (!aname) {
			rc = -1;
			break;
		}

		char **tmp = realloc(newlist, (++nentries + 1) * sizeof (*newlist));
		if (!tmp) {
			rc = -1;
			break;
		}

		tmp[nentries] = NULL;
		tmp[nentries-1] = aname;

		newlist = tmp;
	}
	if (rc == 0 && newlist) {
		qsort(newlist, nentries, sizeof (char *), cmpstringp);
		*namelist = newlist;
	} else {
		if (newlist) {
			for (i = 0; newlist[i] != NULL; i++)
				free(newlist[i]);
			free(newlist);
		}
	}
	return rc;
}

int
read_boot_var_names(char ***namelist)
{
	return read_var_names(select_boot_var_names, namelist);
}

static int
get_edd_version()
{
	efi_guid_t guid = BLKX_UNKNOWN_GUID;
	uint8_t *data = NULL;
	size_t data_size = 0;
	uint32_t attributes;
	ACPI_DEVICE_PATH *path;
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

	rc = efi_get_variable(guid, "blk0", &data, &data_size, &attributes);
	if (rc < 0)
		return rc;

	path = (ACPI_DEVICE_PATH *)data;
	if (path->type == 2 && path->subtype == 1)
		return 3;
	return 1;
}

/*
  EFI_DEVICE_PATH, 0x01 (Hardware), 0x04 (Vendor), length 0x0018
  This needs to know what EFI device has the boot device.
*/
static ssize_t
make_edd10_device_path(uint32_t hardware_device, uint8_t *buf, size_t size)
{
	VENDOR_DEVICE_PATH *hw;
	char buffer[EDD10_HARDWARE_VENDOR_PATH_LENGTH];
	efi_guid_t guid = EDD10_HARDWARE_VENDOR_PATH_GUID;
	uint32_t *data;
	memset(buffer, 0, sizeof(buffer));
	hw = (VENDOR_DEVICE_PATH *)buffer;
	data = (uint32_t *)hw->data;
	hw->type = 0x01; /* Hardware Device Path */
	hw->subtype = 0x04; /* Vendor */
	hw->length = EDD10_HARDWARE_VENDOR_PATH_LENGTH;
	memcpy(&(hw->vendor_guid), &guid, sizeof(guid));
	*data = hardware_device;
	if (size >= hw->length)
		memcpy(buf, buffer, hw->length);
	return hw->length;
}

static ssize_t
make_end_device_path(uint8_t *buf, size_t size)
{
	END_DEVICE_PATH p;
	memset(&p, 0, sizeof(p));
	p.type = 0x7F; /* End of Hardware Device Path */
	p.subtype = 0xFF; /* End Entire Device Path */
	p.length = sizeof(p);
	if (size >= p.length)
		memcpy(buf, &p, p.length);
	return p.length;
}

static ssize_t
make_acpi_device_path(uint32_t _HID, uint32_t _UID, uint8_t *buf, size_t size)
{
	ACPI_DEVICE_PATH p;
	memset(&p, 0, sizeof(p));
	p.type = 2;
	p.subtype = 1;
	p.length = sizeof(p);
	p._HID = _HID;
	p._UID = _UID;
	if (size >= p.length)
		memcpy(buf, &p, p.length);
	return p.length;
}

static ssize_t
make_mac_addr_device_path(char *mac, uint8_t iftype, uint8_t *buf, size_t size)
{
	int i;
	MAC_ADDR_DEVICE_PATH p;
	memset(&p, 0, sizeof(p));
	p.type = 3;
	p.subtype = 11;
	p.length = sizeof(p);
	for (i=0; i < 14; i++) {
		p.macaddr[i] = mac[i];
	}
	p.iftype = iftype;
	if (size >= p.length)
		memcpy(buf, &p, p.length);
	return p.length;
}

struct device
{
	struct pci_dev *pci_dev;
	struct list_head node;
};

static struct device *
is_parent_bridge(struct pci_dev *p, unsigned int target_bus)
{
	struct device *d;
 	unsigned int primary __attribute__((unused)), secondary;

	if ( (pci_read_word(p, PCI_HEADER_TYPE) & 0x7f) != PCI_HEADER_TYPE_BRIDGE)
		return NULL;

	primary=pci_read_byte(p, PCI_PRIMARY_BUS);
	secondary=pci_read_byte(p, PCI_SECONDARY_BUS);


	if (secondary != target_bus)
		return NULL;

	d = malloc(sizeof(struct device));
	if (!d)
		return NULL;
	memset(d, 0, sizeof(*d));
	INIT_LIST_HEAD(&d->node);

	d->pci_dev = p;

	return d;
}

static struct device *
find_parent(struct pci_access *pacc, unsigned int target_bus)
{
	struct device *dev;
	struct pci_dev *p;

	for (p=pacc->devices; p; p=p->next) {
		dev = is_parent_bridge(p, target_bus);
		if (dev)
			return dev;
	}
	return NULL;
}

static ssize_t
make_one_pci_device_path(uint8_t device, uint8_t function,
			uint8_t *buf, size_t size)
{
	PCI_DEVICE_PATH p;
	memset(&p, 0, sizeof(p));
	p.type = 1;
	p.subtype = 1;
	p.length   = sizeof(p);
	p.device   = device;
	p.function = function;
	if (size >= p.length)
		memcpy(buf, &p, p.length);
	return p.length;
}

static ssize_t
make_pci_device_path(uint8_t bus, uint8_t device, uint8_t function,
			uint8_t *buf, size_t size)
{
	struct device *dev;
	struct pci_access *pacc;
	struct list_head *pos, *n;
	LIST_HEAD(pci_parent_list);
	ssize_t needed;
	off_t buf_offset = 0;

	pacc = pci_alloc();
	if (!pacc)
		return 0;

	pci_init(pacc);
	pci_scan_bus(pacc);

	do {
		dev = find_parent(pacc, bus);
		if (dev) {
			list_add(&pci_parent_list, &dev->node);
			bus = dev->pci_dev->bus;
		}
	} while (dev && bus);

	list_for_each_safe(pos, n, &pci_parent_list) {
		dev = list_entry(pos, struct device, node);
		needed = make_one_pci_device_path(dev->pci_dev->dev,
					dev->pci_dev->func,
					buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
		if (needed < 0)
			return -1;
		buf_offset += needed;
		list_del(&dev->node);
		free(dev);
	}

	needed = make_one_pci_device_path(device, function, buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return -1;
	buf_offset += needed;

	pci_cleanup(pacc);

	return buf_offset;
}

static ssize_t
make_nvme_device_path(uint32_t ns_id, uint8_t *buf, size_t size)
{
	NVME_DEVICE_PATH p;
	memset(&p, 0, sizeof(p));
	p.type = 3;
	p.subtype = 23;
	p.length = sizeof(p);
	p.namespace_id = ns_id;
	if (size >= p.length)
		memcpy(buf, &p, p.length);
	return p.length;
}

static ssize_t
make_scsi_device_path(uint16_t id, uint16_t lun, uint8_t *buf, size_t size)
{
	SCSI_DEVICE_PATH p;
	memset(&p, 0, sizeof(p));
	p.type = 3;
	p.subtype = 2;
	p.length   = sizeof(p);
	p.id       = id;
	p.lun      = lun;
	if (size >= p.length)
		memcpy(buf, &p, p.length);
	return p.length;
}

static ssize_t
make_harddrive_device_path(uint32_t num, uint64_t part_start,
			uint64_t part_size, uint8_t *signature,
			uint8_t mbr_type, uint8_t signature_type,
			uint8_t *buf, size_t size)
{
	HARDDRIVE_DEVICE_PATH p;
	memset(&p, 0, sizeof(p));
	p.type = 4;
	p.subtype = 1;
	p.length   = sizeof(p);
	p.part_num = num;
	p.start = part_start;
	p.size = part_size;
	if (signature) memcpy(p.signature, signature, 16);
	p.mbr_type = mbr_type;
	p.signature_type = signature_type;
	if (size >= p.length)
		memcpy(buf, &p, p.length);
	return p.length;
}

static ssize_t
make_file_path_device_path(efi_char16_t *name, uint8_t *buf, size_t size)
{
	FILE_PATH_DEVICE_PATH *p;
	int namelen  = efichar_strlen(name, -1);
	int namesize = efichar_strsize(name);
	char *buffer = calloc(1, sizeof (*p) + namesize);
	ssize_t ret;

	if (!buffer)
		return -1;

	memset(buffer, 0, namesize + 1);
	p = (FILE_PATH_DEVICE_PATH *)buffer;
	p->type      = 4;
	p->subtype   = 4;
	p->length    = 4 + namesize;
	efichar_strncpy(p->path_name, name, namelen);

	if (size >= p->length)
		memcpy(buf, buffer, p->length);
	ret = p->length;
	free(buffer);
	return ret;
}

static ssize_t
make_edd30_device_path(int fd, uint8_t *buf, size_t size)
{
	int rc=0, interface_type;
	unsigned char bus=0, device=0, function=0;
	Scsi_Idlun idlun;
	uint32_t ns_id;
	unsigned char host=0, channel=0, id=0, lun=0;
	ssize_t needed;
	off_t buf_offset = 0;

	rc = disk_get_pci(fd, &interface_type, &bus, &device, &function);
	if (rc) return 0;
	if (interface_type == nvme) {
		rc = get_nvme_ns_id(fd, &ns_id);
		if (rc)
			return 0;
	} else if (interface_type != virtblk) {
		memset(&idlun, 0, sizeof(idlun));
		rc = get_scsi_idlun(fd, &idlun);
		if (rc) return 0;
		idlun_to_components(&idlun, &host, &channel, &id, &lun);
	}

	needed = make_acpi_device_path(EISAID_PNP0A03, bus, buf, size);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	needed = make_pci_device_path(bus, device, function, buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	if (interface_type == nvme) {
		needed = make_nvme_device_path(ns_id, buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
		if (needed < 0)
			return needed;
		buf_offset += needed;
	} else if (interface_type != virtblk) {
		needed = make_scsi_device_path(id, lun, buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
		if (needed < 0)
			return needed;
		buf_offset += needed;
	}

	return buf_offset;
}

static char *
tilt_slashes(char *s)
{
	char *p;
	for (p = s; *p; p++)
		if (*p == '/')
			*p = '\\';
	return s;
}

/**
 * make_disk_load_option()
 * @disk disk
 * @buf - load option returned
 * @size - size of buffer available
 *
 * Returns -1 on error, length of load option created on success.
 */
static ssize_t
make_disk_load_option(char *disk, uint8_t *buf, size_t size)
{
	int disk_fd=0;
	char signature[16];
	int rc, edd_version=0;
	uint8_t mbr_type=0, signature_type=0;
	uint64_t part_start=0, part_size=0;
	efi_char16_t *os_loader_path;
	ssize_t needed = 0;
	off_t buf_offset = 0;

	memset(signature, 0, sizeof(signature));

	disk_fd = open(opts.disk, O_RDWR);
	if (disk_fd == -1)
		err(5, "Could not open disk %s", opts.disk);

	if (opts.edd_version) {
		edd_version = get_edd_version();
		if (edd_version == 3) {
			needed = make_edd30_device_path(disk_fd, buf, size);
		} else if (edd_version == 1) {
			needed = make_edd10_device_path(opts.edd10_devicenum,
							buf, size);
		}
		if (needed < 0) {
			close(disk_fd);
			return needed;
		}
		buf_offset += needed;
	}

	rc = disk_get_partition_info(disk_fd, opts.part,
				  &part_start, &part_size, signature,
				  &mbr_type, &signature_type);
	close(disk_fd);
	if (rc)
		errx(5, "No partition information on disk %s.\n"
			"Cowardly refusing to create a boot option.\n",
			opts.disk);

	needed = make_harddrive_device_path(opts.part, part_start, part_size,
					(uint8_t *)signature, mbr_type,
					signature_type, buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	needed = (strlen(opts.loader) + 1) * sizeof (*os_loader_path);
	os_loader_path = malloc(needed);
	if (!os_loader_path)
		return -1;
	efichar_from_char(os_loader_path, tilt_slashes(opts.loader),
			needed);
	needed = make_file_path_device_path(os_loader_path, buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	free(os_loader_path);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	needed = make_end_device_path(buf + buf_offset,
				size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	return buf_offset;
}

static int
get_virt_pci(char *name, unsigned char *bus,
		unsigned char *device, unsigned char *function)
{
	char inbuf[64], outbuf[128];
	ssize_t lnksz;

	if (snprintf(inbuf, sizeof inbuf, "/sys/bus/virtio/devices/%s",
			name) >= (ssize_t)(sizeof inbuf)) {
		return -1;
	}

	lnksz = readlink(inbuf, outbuf, sizeof outbuf);
	if (lnksz == -1 || lnksz == sizeof outbuf) {
		return -1;
	}

	outbuf[lnksz] = '\0';
	if (sscanf(outbuf, "../../../devices/pci0000:00/0000:%hhx:%hhx.%hhx",
			bus, device, function) != 3) {
		return -1;
	}
	return 0;
}

/**
 * make_net_load_option()
 * @iface - interface name (input)
 * @buf - buffer to write structure to
 * @size - size of buf
 *
 * Returns -1 on error, size written on success, or size needed if size == 0.
 */
static ssize_t
make_net_load_option(char *iface, uint8_t *buf, size_t size)
{
	/* copied pretty much verbatim from the ethtool source */
	int fd = 0, err;
	unsigned char bus, slot, func;
	struct ifreq ifr;
	struct ethtool_drvinfo drvinfo;
	ssize_t needed;
	off_t buf_offset;

	memset(&ifr, 0, sizeof(ifr));
	strcpy(ifr.ifr_name, iface);
	drvinfo.cmd = ETHTOOL_GDRVINFO;
	ifr.ifr_data = (caddr_t)&drvinfo;
	/* Open control socket */
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("Cannot get control socket");
		return -1;
	}
	err = ioctl(fd, SIOCETHTOOL, &ifr);
	if (err < 0) {
		perror("Cannot get driver information");
		close(fd);
		return -1;
	}

	if (strncmp(drvinfo.bus_info, "virtio", 6) == 0) {
		err = get_virt_pci(drvinfo.bus_info, &bus, &slot, &func);
		if (err < 0) {
			close(fd);
			return err;
		}
	} else {
		/* The domain part was added in 2.6 kernels.
		 * Test for that first. */
		err = sscanf(drvinfo.bus_info, "%*x:%hhx:%hhx.%hhx",
						&bus, &slot, &func);
		if (err != 3) {
			err = sscanf(drvinfo.bus_info, "%hhx:%hhx.%hhx",
						&bus, &slot, &func);
			if (err != 3) {
				perror("Couldn't parse device location string.");
				close(fd);
				return -1;
			}
		}
	}

	err = ioctl(fd, SIOCGIFHWADDR, &ifr);
	if (err < 0) {
		close(fd);
		perror("Cannot get hardware address.");
		return -1;
	}
	close(fd);

	buf_offset = 0;
	needed = make_acpi_device_path(opts.acpi_hid, opts.acpi_uid, buf,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	needed = make_pci_device_path(bus, (uint8_t)slot, (uint8_t)func,
					buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	needed = make_mac_addr_device_path(ifr.ifr_ifru.ifru_hwaddr.sa_data,
					ifr.ifr_ifru.ifru_hwaddr.sa_family,
					buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	needed = make_end_device_path(buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	return buf_offset;
}

#define extend(buf, oldsize, size) ((void *)({				\
		typeof(buf) __tmp = realloc(buf, (oldsize) + (size));	\
		if (!__tmp) {						\
			free(buf);					\
			return -1;					\
		}							\
		(oldsize) += (size);					\
		buf = __tmp;						\
	}))

/**
 * make_linux_load_option()
 * @data - load option returned
 * *data_size - load option size returned
 *
 * Returns 0 on error, length of load option created on success.
 */
ssize_t
make_linux_load_option(uint8_t **data, size_t *data_size)
{
	EFI_LOAD_OPTION *load_option = NULL;
	size_t load_option_size = sizeof (*load_option);
	efi_char16_t description[64];
	uint8_t *buf;
	ssize_t needed;
	off_t buf_offset = 0, desc_offset;
	int rc;

	load_option = calloc(1, sizeof (*load_option));
	if (load_option == NULL) {
		fprintf(stderr, "efibootmgr: %m\n");
		return -1;
	}
	buf = (uint8_t *)load_option;
	buf_offset = 0;

	/* Write Attributes */
	if (opts.active)
		load_option->attributes = LOAD_OPTION_ACTIVE;
	else
		load_option->attributes = 0;
	buf_offset += sizeof(uint32_t);

	/* skip writing file_path_list_length */
	buf_offset += sizeof(uint16_t);
	/* Write description.  This is the text that appears on the screen for the load option. */
	memset(description, 0, sizeof(description));
	efichar_from_char(description, opts.label, sizeof(description));

	needed = (strlen(opts.label) + 1) * 2;
	buf = extend(load_option, load_option_size, needed);

	efichar_strncpy(load_option->description, description,
			efichar_strlen(description, -1) + 1);
	buf_offset += needed;
	desc_offset = buf_offset;

	if (opts.iface) {
		needed = make_net_load_option(opts.iface, NULL, 0);
		if (needed < 0) {
			fprintf(stderr, "efibootmgr: could not create load option: %m\n");
			free(buf);
			return needed;
		}
		buf = extend(load_option, load_option_size, needed);
		rc = make_net_load_option(opts.iface, buf + buf_offset, needed);
		buf_offset += needed;
		if (rc < 0) {
			fprintf(stderr, "efibootmgr: could not create load option: %m\n");
			free(buf);
			return rc;
		}
	} else {
		needed = make_disk_load_option(opts.iface, NULL, 0);
		if (needed < 0) {
			fprintf(stderr, "efibootmgr: could not create load option: %m\n");
			free(buf);
			return needed;
		}
		buf = extend(load_option, load_option_size, needed);
		rc = make_disk_load_option(opts.iface, buf + buf_offset, needed);
		buf_offset += needed;
		if (rc < 0) {
			fprintf(stderr, "efibootmgr: could not create load option: %m\n");
			free(buf);
			return rc;
		}
	}

	load_option->file_path_list_length = buf_offset - desc_offset;

	*data_size = buf_offset;
	*data = (uint8_t *)load_option;
	return *data_size;
}

/*
 * append_extra_args()
 * appends all arguments from argv[] not snarfed by getopt
 * as one long string onto data.
 */
static int
append_extra_args_ascii(uint8_t **data, size_t *data_size)
{
	uint8_t *new_data = NULL;
	char *p;
	int i;
	unsigned long usedchars=0;

	if (!data || *data) {
		errno = EINVAL;
		return -1;
	}

	for (i=opts.optind; i < opts.argc; i++)	{
		int l = strlen(opts.argv[i]);
		int space = (i < opts.argc - 1) ? 1: 0;
		uint8_t *tmp = realloc(new_data, (usedchars + l + space + 1));
		if (tmp == NULL)
			return -1;
		new_data = tmp;
		p = (char *)new_data + usedchars;
		strcpy(p, opts.argv[i]);
		usedchars += l;
		/* Put a space between args */
		if (space)
			new_data[usedchars++] = ' ';
		new_data[usedchars] = '\0';
	}

	if (!new_data)
		return 0;

	*data = (uint8_t *)new_data;
	*data_size = usedchars;

	return 0;
}

static int
append_extra_args_unicode(uint8_t **data, size_t *data_size)
{
	uint16_t *new_data = NULL, *p;
	int i;
	unsigned long usedchars=0;

	if (!data || *data) {
		errno = EINVAL;
		return -1;
	}

	for (i = opts.optind; i < opts.argc; i++) {
		int l = strlen(opts.argv[i]) + 1;
		int space = (i < opts.argc - 1) ? 1 : 0;
		uint16_t *tmp = realloc(new_data, (usedchars + l + space)
						  * sizeof (*new_data));
		if (tmp == NULL)
			return -1;
		new_data = tmp;
		p = new_data + usedchars;
		usedchars += efichar_from_char((efi_char16_t *)p,
						opts.argv[i], l * 2)
				/ sizeof (*new_data);
		p = new_data + usedchars;
		/* Put a space between args */
		if (space)
			usedchars += efichar_from_char(
						(efi_char16_t *)p, " ", 2)
				/ sizeof (*new_data);
	}

	if (!new_data)
		return 0;

	*data = (uint8_t *)new_data;
	*data_size = usedchars * sizeof (*new_data);

	return 0;
}

static int
append_extra_args_file(uint8_t **data, size_t *data_size)
{
	char *file = opts.extra_opts_file;
	int fd = STDIN_FILENO;
	ssize_t num_read=0;
	unsigned long appended=0;
	size_t maxchars = 0;
	char *buffer;

	if (!data || *data) {
		errno = EINVAL;
		return -1;
	}

	if (file && strncmp(file, "-", 1))
		fd = open(file, O_RDONLY);

	if (fd < 0)
		return -1;

	buffer = malloc(maxchars);
	do {
		if (maxchars - appended == 0) {
			maxchars += 1024;
			char *tmp = realloc(buffer, maxchars);
			if (tmp == NULL)
				return -1;
			buffer = tmp;
		}
		num_read = read(fd, buffer + appended, maxchars - appended);
		if (num_read < 0)
			return -1;
		else if (num_read > 0)
			appended += num_read;
	} while (num_read > 0);

	if (fd != STDIN_FILENO)
		close(fd);

	*data = (uint8_t *)buffer;
	*data_size = appended;

	return appended;
}

static int
add_new_data(uint8_t **data, size_t *data_size,
		uint8_t *new_data, size_t new_data_size)
{
	uint8_t *tmp = realloc(*data, *data_size + new_data_size);
	if (tmp == NULL)
		return -1;
	memcpy(tmp + *data_size, new_data, new_data_size);
	*data = tmp;
	*data_size = *data_size + new_data_size;
	return 0;
}

int
append_extra_args(uint8_t **data, size_t *data_size)
{
	int ret = 0;
	uint8_t *new_data = NULL;
	size_t new_data_size = 0;

	if (opts.extra_opts_file) {
		ret = append_extra_args_file(&new_data, &new_data_size);
		if (ret < 0) {
			fprintf(stderr, "efibootmgr: append_extra_args: %m\n");
			return -1;
		}
	}
	if (new_data_size) {
		ret = add_new_data(data, data_size, new_data, new_data_size);
		free(new_data);
		if (ret < 0) {
			fprintf(stderr, "efibootmgr: append_extra_args: %m\n");
			return -1;
		}
		new_data = NULL;
		new_data_size = 0;
	}

	if  (opts.unicode)
		ret = append_extra_args_unicode(&new_data, &new_data_size);
	else
		ret = append_extra_args_ascii(&new_data, &new_data_size);
	if (ret < 0) {
		fprintf(stderr, "efibootmgr: append_extra_args: %m\n");
		if (new_data) /* this can't happen, but covscan believes */
			free(new_data);
		return -1;
	}
	if (new_data_size) {
		ret = add_new_data(data, data_size, new_data, new_data_size);
		free(new_data);
		new_data = NULL;
		if (ret < 0) {
			fprintf(stderr, "efibootmgr: append_extra_args: %m\n");
			return -1;
		}
		new_data_size = 0;
	}

	if (new_data) /* once again, this can't happen, but covscan believes */
		free(new_data);
	return 0;
}
