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

static int
get_path_options(void)
{
	switch (opts.abbreviate_path)
	{
	case EFIBOOTMGR_PATH_ABBREV_EDD10:
		/* EDD 1.0 */
		return EFIBOOT_ABBREV_EDD10;
	case EFIBOOTMGR_PATH_ABBREV_NONE:
		/* EDD 3.0+, which we actually just ignore, because we don't
		 * actually *have* edd, and can't actually derive a path from
		 * anything. */
		return EFIBOOT_ABBREV_NONE;
	case EFIBOOTMGR_PATH_ABBREV_FILE:
		/* Abbreviate to a file path */
		return EFIBOOT_ABBREV_FILE;
	case EFIBOOTMGR_PATH_ABBREV_UNSPECIFIED:
	case EFIBOOTMGR_PATH_ABBREV_HD:
	default:
		/* Abbreviate to an HD path */
		return EFIBOOT_ABBREV_HD;
	}
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
	uint32_t attributes = opts.active ? LOAD_OPTION_ACTIVE : 0
			    | (opts.reconnect > 0 ? LOAD_OPTION_FORCE_RECONNECT : 0);
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
		uint32_t options;

		options = get_path_options();

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
