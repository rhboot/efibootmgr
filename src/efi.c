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

#include "fix_coverity.h"

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <efiboot.h>
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
#include <asm/types.h>
#include <linux/ethtool.h>
#include "efi.h"
#include "efibootmgr.h"
#include "list.h"

static int
select_var_names_by_prefix(const efi_guid_t *guid, const char *prefix,
			   const char *name)
{
	efi_guid_t global = EFI_GLOBAL_GUID;
	size_t plen = strlen(prefix);
	const char *num = name + plen;
	if (!strncmp(name, prefix, plen) &&
			isxdigit(num[0]) && isxdigit(num[1]) &&
			isxdigit(num[2]) && isxdigit(num[3]) &&
			!memcmp(guid, &global, sizeof (global)))
		return 1;
	return 0;
}
typedef __typeof__(select_var_names_by_prefix) filter_t;

static int
cmpstringp(const void *p1, const void *p2)
{
	const char *s1 = *(const char **)p1;
	const char *s2 = *(const char **)p2;
	return strcoll(s1, s2);
}

static int
read_prefixed_var_names(filter_t filter, const char *prefix, char ***namelist)
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
		if (!filter(guid, prefix, name))
			continue;

		char *aname = strdup(name);
		if (!aname) {
			rc = -1;
			break;
		}

		char **tmp = realloc(newlist, (++nentries + 1) * sizeof (*newlist));
		if (!tmp) {
			free(aname);
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
read_var_names(const char *prefix, char ***namelist)
{
	return read_prefixed_var_names(select_var_names_by_prefix,
				       prefix, namelist);
}

int
read_boot_var_names(char ***namelist)
{
	return read_var_names("Boot", namelist);
}

#if 0
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

	buf_offset = 0;
	needed = efidp_make_acpi_hid(buf, size?size-buf_offset:0,
				     opts.acpi_hid, opts.acpi_uid);
	if (needed < 0) {
err_needed:
		close(fd);
		return needed;
	}
	buf_offset += needed;

	needed = make_pci_device_path(bus, (uint8_t)slot, (uint8_t)func,
					buf + buf_offset,
					size == 0 ? 0 : size - buf_offset);
	if (needed < 0)
		goto err_needed;
	buf_offset += needed;

	needed = efidp_make_mac_addr(buf, size?size-buf_offset:0,
				     ifr.ifr_ifru.ifru_hwaddr.sa_family,
				     (uint8_t*)ifr.ifr_ifru.ifru_hwaddr.sa_data,
				     sizeof (ifr.ifr_ifru.ifru_hwaddr.sa_data));
	if (needed < 0)
		goto err_needed;
	buf_offset += needed;

	if (opts.ipv4) {
		needed = make_ipv4_addr_device_path(fd, );
		if (needed < 0)
			goto err_needed;
		buf_offset += needed;
	}

	if (opts.ipv6) {
		needed = make_ipv6_addr_device_path(fd, );
		if (needed < 0)
			goto err_needed;
		buf_offset += needed;
	}
	close(fd);

	needed = efidp_make_end_entire(buf,size?size-buf_offset:0);
	if (needed < 0)
		return needed;
	buf_offset += needed;

	return buf_offset;
}
#endif

static int
get_edd_version(void)
{
	efi_guid_t guid = BLKX_UNKNOWN_GUID;
	uint8_t *data = NULL;
	size_t data_size = 0;
	uint32_t attributes;
	efidp_header *path;
	int rc = 0;

	/* Allow global user option override */

	switch (opts.edd_version)
	{
	case 0: /* No EDD information */
		return 0;
	case 1: /* EDD 1.0 */
		return 1;
	case 3: /* EDD 3.0 */
		return 3;
	default:
		break;
	}

	rc = efi_get_variable(guid, "blk0", &data, &data_size, &attributes);
	if (rc < 0)
		return rc;

	path = (efidp_header *)data;
	if (path->type == 2 && path->subtype == 1)
		return 3;
	return 1;
}


/**
 * make_linux_load_option()
 * @data - load option returned
 * *data_size - load option size returned
 *
 * Returns 0 on error, length of load option created on success.
 */
ssize_t
make_linux_load_option(uint8_t **data, size_t *data_size,
		       uint8_t *optional_data, size_t optional_data_size)
{
	ssize_t needed;
	uint32_t attributes = opts.active ? LOAD_OPTION_ACTIVE : 0;
	int saved_errno;
	efidp dp = NULL;

	if (opts.iface && opts.ip_version == EFIBOOTMGR_IPV4) {
		needed = efi_generate_ipv4_device_path(NULL, 0, opts.iface,
						       opts.local_ip_addr,
						       opts.remote_ip_addr,
						       opts.gateway_ip_addr,
						       opts.ip_netmask,
						       opts.ip_local_port,
						       opts.ip_remote_port,
						       opts.ip_protocol,
						       opts.ip_addr_origin);
		if (needed < 0) {
			efi_error("efi_generate_ipv4_device_path() = %zd (failed)",
					needed);
			return -1;
		}
		if (data_size && *data_size) {
			dp = malloc(needed);

			needed = efi_generate_ipv4_device_path(
							(uint8_t *)dp, needed,
							opts.iface,
							opts.local_ip_addr,
							opts.remote_ip_addr,
							opts.gateway_ip_addr,
							opts.ip_netmask,
							opts.ip_local_port,
							opts.ip_remote_port,
							opts.ip_protocol,
							opts.ip_addr_origin);
			if (needed < 0) {
				free(dp);
				efi_error("efi_generate_ipv4_device_path() = %zd (failed)",
						needed);
				return -1;
			}
		}
	} else if (opts.iface && opts.ip_version == EFIBOOTMGR_IPV6) {
		errno = ENOSYS;
		return -1;
	} else {
		uint32_t options = EFIBOOT_ABBREV_HD;
		int edd;

		/* there's really no telling if this is even the right disk,
		 * but... I also never see blk0 exported to runtime on any
		 * hardware, so it probably only happens on some old itanium
		 * box from the beginning of time anyway. */
		edd = get_edd_version();

		switch (edd) {
		case 1:
			options = EFIBOOT_ABBREV_EDD10;
			break;
		case 3:
			options = EFIBOOT_ABBREV_NONE;
			break;
		}

		needed = efi_generate_file_device_path_from_esp(NULL, 0,
						opts.disk, opts.part,
						opts.loader, options,
						opts.edd10_devicenum);
		if (needed < 0) {
			efi_error("efi_generate_file_device_path_from_esp() = %zd (failed)",
                                  needed);
			return -1;
		}

		if (data_size && *data_size) {
			dp = malloc(needed);
			if (dp == NULL)
				return -1;
			needed = efi_generate_file_device_path_from_esp(
						(uint8_t *)dp, needed,
						opts.disk, opts.part,
						opts.loader, options,
						opts.edd10_devicenum);
			if (needed < 0) {
			        efi_error("efi_generate_file_device_path_from_esp() = %zd (failed)",
                                          needed);
				free(dp);
				return -1;
			}
		}
	}

	size_t data_size_tmp = 0;
	if (data_size)
		data_size_tmp = *data_size;
	needed = efi_loadopt_create(*data, data_size_tmp,
				      attributes, dp, needed, opts.label,
				      optional_data, optional_data_size);
	if (dp) {
		saved_errno = errno;
		free(dp);
		dp = NULL;
		errno = saved_errno;
	}
	if (needed < 0) {
		efi_error("efi_loadopt_create() = %zd (failed)", needed);
		return -1;
        }

	return needed;
}

static ssize_t
read_stdin(uint8_t *data_out, ssize_t data_size_out)
{
	static uint8_t *data = NULL;
	static ssize_t data_size = 0;
	off_t pos = 0;
	ssize_t allocated;

	if (data_out && data_size_out) {
		if (!data || data_size != data_size_out) {
			errno = EINVAL;
			return -1;
		}
		memcpy(data_out, data, data_size);
		return data_size;
	}
	allocated = 4096;
	data = malloc(allocated);
	if (!data)
		return -1;
	memset(data, 0, allocated);

	while (1) {
		ssize_t ret;
		if (allocated - pos == 0) {
			allocated += 4096;
			/*
			 * there's really no way a variable is going to be
			 * 64k and work, so bail before we suck up all of
			 * memory.
			 */
			if (allocated > 4096 * 16) {
				errno = ENOSPC;
err:
				free(data);
				data = 0;
				data_size = 0;
				return -1;
			}

			uint8_t *data_new;
			data_new = realloc(data, allocated);
			if (!data_new)
				goto err;
			data = data_new;
		}
		ret = fread(data+pos, 1, allocated-pos, stdin);
		if (ret == 0) {
			if (ferror(stdin)) {
				errno = EIO;
				goto err;
			}
			if (feof(stdin))
				break;
		}
		data_size += ret;
		pos += ret;
	}
	return data_size;
}

ssize_t
get_extra_args(uint8_t *data, ssize_t data_size)
{
	int i;
	ssize_t needed = 0, sz;
	off_t off = 0;

	if (opts.extra_opts_file) {
		if (!strcmp(opts.extra_opts_file, "-"))
			needed = read_stdin(data, data_size);
		else
			needed = efi_loadopt_args_from_file(data, data_size,
						     opts.extra_opts_file);
		if (needed < 0)
			fprintf(stderr, "efibootmgr: get_extra_args: %m\n");
		return needed;
	}
	for (i = opts.optind; i < opts.argc; i++) {
		int space = (i < opts.argc - 1) ? 1 : 0;

		if (opts.unicode) {
			sz = efi_loadopt_args_as_ucs2(
						(uint16_t *)(data+off),
						data_size?data_size+off:0,
						(uint8_t *)opts.argv[i]);
			if (sz < 0)
				return -1;
			off += sz;
			if (data && off < data_size-2 && space) {
				data[off] = '\0';
				data[off+1] = '\0';
			}
			off += space * sizeof (uint16_t);
		} else {
			sz = efi_loadopt_args_as_utf8(data+off,
						data_size?data_size+off:0,
						(uint8_t *)opts.argv[i]);
			if (sz < 0)
				return -1;
			off += sz;
			if (data && off < data_size-1 && space) {
				data[off] = '\0';
			}
			off += space;
		}
		needed += off;
	}
	return needed;
}
