/*
  efibootmgr.c - Manipulates EFI variables as exported in /proc/efi/vars

  Copyright (C) 2001-2004 Dell, Inc. <Matt_Domsch@dell.com>

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


  This must tie the EFI_DEVICE_PATH to /boot/efi/EFI/redhat/grub.efi
  The  EFI_DEVICE_PATH will look something like:
    ACPI device path, length 12 bytes
    Hardware Device Path, PCI, length 6 bytes
    Messaging Device Path, SCSI, length 8 bytes, or ATAPI, length ??
    Media Device Path, Hard Drive, partition XX, length 30 bytes
    Media Device Path, File Path, length ??
    End of Hardware Device Path, length 4
    Arguments passed to elilo, as UCS-2 characters, length ??

*/

#define _GNU_SOURCE

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <getopt.h>
#include <efivar.h>
#include <inttypes.h>
#include "list.h"
#include "efi.h"
#include "efichar.h"
#include "unparse_path.h"
#include "disk.h"
#include "efibootmgr.h"


#ifndef EFIBOOTMGR_VERSION
#define EFIBOOTMGR_VERSION "unknown (fix Makefile!)"
#endif


typedef struct _var_entry {
	char		*name;
	efi_guid_t	guid;
	uint8_t		*data;
	size_t		data_size;
	uint32_t	attributes;
	uint16_t	num;
	list_t		list;
} efi_variable_t;

/* global variables */
static	LIST_HEAD(boot_entry_list);
static	LIST_HEAD(blk_list);
efibootmgr_opt_t opts;

static void
free_vars(list_t *head)
{
	list_t *pos, *n;
	efi_variable_t *boot;

	list_for_each_safe(pos, n, head) {
		boot = list_entry(pos, efi_variable_t, list);
		if (boot->data)
			free(boot->data);
		list_del(&(boot->list));
		free(boot);
	}
}

static void
read_vars(char **namelist,
	  list_t *head)
{
	efi_variable_t *entry;
	int i, rc;

	if (!namelist)
		return;

	for (i=0; namelist[i] != NULL; i++) {
		if (namelist[i]) {
			entry = malloc(sizeof(efi_variable_t));
			if (!entry)
				goto err;
			memset(entry, 0, sizeof(efi_variable_t));

			rc = efi_get_variable(EFI_GLOBAL_GUID, namelist[i],
					       &entry->data, &entry->data_size,
					       &entry->attributes);
			if (rc < 0)
				goto err;

			/* latest apple firmware sets high bit which appears
			 * invalid to the linux kernel if we write it back so
			 * lets zero it out if it is set since it would be
			 * invalid to set it anyway */
			entry->attributes = entry->attributes & ~(1 << 31);

			entry->name = namelist[i];
			list_add_tail(&entry->list, head);
		}
	}
	return;
err:
	warn("efibootmgr");
	exit(1);
}

static void
free_array(char **array)
{
	int i;

	if (!array)
		return;

	for (i = 0; array[i] != NULL; i++)
		free(array[i]);

	free(array);
}

static int
compare(const void *a, const void *b)
{
	int rc = -1;
	uint16_t n1, n2;
	memcpy(&n1, a, sizeof(n1));
	memcpy(&n2, b, sizeof(n2));
	if (n1 < n2) rc = -1;
	if (n1 == n2) rc = 0;
	if (n1 > n2) rc = 1;
	return rc;
}


/*
  Return an available boot variable number,
  or -1 on failure.
*/
static int
find_free_boot_var(list_t *boot_list)
{
	int num_vars=0, i=0, found;
	uint16_t *vars, free_number;
	list_t *pos;
	efi_variable_t *boot;
	list_for_each(pos, boot_list) {
		num_vars++;
	}
	vars = calloc(1, sizeof(uint16_t) * num_vars);
	if (!vars)
		return -1;

	list_for_each(pos, boot_list) {
		boot = list_entry(pos, efi_variable_t, list);
		vars[i] = boot->num;
		i++;
	}
	qsort(vars, i, sizeof(uint16_t), compare);
	found = 1;

	num_vars = i;
	for (free_number = 0; free_number < num_vars && found; free_number++) {
		found = 0;
		list_for_each(pos, boot_list) {
			boot = list_entry(pos, efi_variable_t, list);
			if (boot->num == free_number) {
				found = 1;
				break;
			}
		}
		if (!found) break;
	}
	if (found && num_vars)
		free_number = vars[num_vars-1] + 1;
	free(vars);
	return free_number;
}


static void
warn_duplicate_name(list_t *boot_list)
{
	list_t *pos;
	efi_variable_t *boot;
	EFI_LOAD_OPTION *load_option;

	list_for_each(pos, boot_list) {
		boot = list_entry(pos, efi_variable_t, list);
		load_option = (EFI_LOAD_OPTION *)
			boot->data;
		if (!efichar_char_strcmp(opts.label,
					 load_option->description)) {
			fprintf(stderr, "** Warning ** : %.8s has same label %s\n",
			       boot->name,
			       opts.label);
		}
	}
}

static efi_variable_t *
make_boot_var(list_t *boot_list)
{
	efi_variable_t *boot = NULL;
	int free_number;
	list_t *pos;
	int rc;

	if (opts.bootnum == -1)
		free_number = find_free_boot_var(boot_list);
	else {
		list_for_each(pos, boot_list) {
			boot = list_entry(pos, efi_variable_t, list);
			if (boot->num == opts.bootnum) {
				fprintf(stderr, "** Warning ** : bootnum %04X "
				        "already exists\n", opts.bootnum);
				return NULL;
			}
		}
		free_number = opts.bootnum;
	}

	if (free_number == -1) {
		warn("efibootmgr: no available boot variables");
		return NULL;
	}

	/* Create a new efi_variable_t object
	   and populate it.
	*/

	boot = calloc(1, sizeof(*boot));
	if (!boot) {
		warn("efibootmgr");
		return NULL;
	}
	if (make_linux_load_option(&boot->data, &boot->data_size) < 0)
		goto err_boot_entry;
	if (append_extra_args(&boot->data, &boot->data_size) < 0)
		goto err_boot_entry;

	boot->num = free_number;
	boot->guid = EFI_GLOBAL_VARIABLE;
	rc = asprintf(&boot->name, "Boot%04X", free_number);
	if (rc < 0) {
		warn("efibootmgr");
		goto err_boot_entry;
	}
	boot->attributes = EFI_VARIABLE_NON_VOLATILE |
			    EFI_VARIABLE_BOOTSERVICE_ACCESS |
			    EFI_VARIABLE_RUNTIME_ACCESS;
	rc = efi_set_variable(boot->guid, boot->name, boot->data,
				boot->data_size, boot->attributes);
	if (rc < 0)
		goto err_boot_entry;
	list_add_tail(&boot->list, boot_list);
	return boot;
err_boot_entry:
	if (boot->name) {
		warn("Could not set variable %s", boot->name);
		free(boot->name);
	} else {
		warn("Could not set variable");
	}
	if (boot->data)
		free(boot->data);
	free(boot);
	return NULL;
}

static int
read_boot_order(efi_variable_t **boot_order)
{
	int rc;
	efi_variable_t *new = NULL, *bo = NULL;

	if (*boot_order == NULL) {
		new = calloc(1, sizeof (**boot_order));
		if (!new)
			return -1;
		*boot_order = bo = new;
	} else {
		bo = *boot_order;
	}

	rc = efi_get_variable(EFI_GLOBAL_GUID, "BootOrder",
				&bo->data, &bo->data_size, &bo->attributes);
	if (rc < 0 && new != NULL) {
		free(new);
		*boot_order = NULL;
		bo = NULL;
	}

	if (bo) {
		/* latest apple firmware sets high bit which appears invalid
		 * to the linux kernel if we write it back so lets zero it out
		 * if it is set since it would be invalid to set it anyway */
		bo->attributes = bo->attributes & ~(1 << 31);
	}
	return rc;
}

static int
set_boot_u16(const char *name, uint16_t num)
{
	return efi_set_variable(EFI_GLOBAL_GUID, name, (uint8_t *)&num,
				sizeof (num), EFI_VARIABLE_NON_VOLATILE |
					      EFI_VARIABLE_BOOTSERVICE_ACCESS |
					      EFI_VARIABLE_RUNTIME_ACCESS);
}

static int
add_to_boot_order(uint16_t num)
{
	efi_variable_t *boot_order = NULL;
	uint64_t new_data_size;
	uint16_t *new_data, *old_data;
	int rc;

	rc = read_boot_order(&boot_order);
	if (rc < 0) {
		if (errno == ENOENT)
			rc = set_boot_u16("BootOrder", num);
		return rc;
	}

	/* We've now got an array (in boot_order->data) of the
	 * boot order.  First add our entry, then copy the old array.
	 */
	old_data = (uint16_t *)boot_order->data;
	new_data_size = boot_order->data_size + sizeof(uint16_t);
	new_data = malloc(new_data_size);
	if (!new_data)
		return -1;

	new_data[0] = num;
	memcpy(new_data+1, old_data, boot_order->data_size);

	/* Now new_data has what we need */
	free(boot_order->data);
	boot_order->data = (uint8_t *)new_data;
	boot_order->data_size = new_data_size;

	rc = efi_set_variable(EFI_GLOBAL_GUID, "BootOrder", boot_order->data,
			boot_order->data_size, boot_order->attributes);
	free(boot_order->data);
	free(boot_order);
	return rc;
}

static int
remove_dupes_from_boot_order(void)
{
	efi_variable_t *boot_order = NULL;
	uint64_t new_data_size;
	uint16_t *new_data, *old_data;
	unsigned int old_i,new_i;
	int rc;

	rc = read_boot_order(&boot_order);
	if (rc < 0) {
		if (errno == ENOENT)
			rc = 0;
		return rc;
	}

	old_data = (uint16_t *)(boot_order->data);
	/* Start with the same size */
	new_data_size = boot_order->data_size;
	new_data = malloc(new_data_size);
	if (!new_data)
		return -1;

	unsigned int old_max = boot_order->data_size / sizeof(*new_data);
	for (old_i = 0, new_i = 0; old_i < old_max; old_i++) {
		int copies = 0;
		unsigned int j;
		for (j = 0; j < new_i; j++) {
			if (new_data[j] == old_data[old_i]) {
				copies++;
				break;
			}
		}
		if (copies == 0) {
			/* Copy this value */
			new_data[new_i] = old_data[old_i];
			new_i++;
		}
	}
	/* Adjust the size if we didn't copy everything. */
	new_data_size = sizeof(new_data[0]) * new_i;

	/* Now new_data has what we need */
	free(boot_order->data);
	boot_order->data = (uint8_t *)new_data;
	boot_order->data_size = new_data_size;
	efi_del_variable(EFI_GLOBAL_GUID, "BootOrder");
	rc = efi_set_variable(EFI_GLOBAL_GUID, "BootOrder", boot_order->data,
				boot_order->data_size, boot_order->attributes);
	free(boot_order->data);
	free(boot_order);
	return rc;
}

static int
remove_from_boot_order(uint16_t num)
{
	efi_variable_t *boot_order = NULL;
	uint64_t new_data_size;
	uint16_t *new_data, *old_data;
	unsigned int old_i,new_i;
	int rc;

	rc = read_boot_order(&boot_order);
	if (rc < 0) {
		if (errno == ENOENT)
			rc = 0;
		return rc;
	}

	/* We've now got an array (in boot_order->data) of the
	   boot order.  Simply copy the array, skipping the
	   entry we're deleting.
	*/
	old_data = (uint16_t *)(boot_order->data);
	/* Start with the same size */
	new_data_size = boot_order->data_size - sizeof (*new_data);
	new_data = malloc(new_data_size);
	if (!new_data)
		return -1;

	for (old_i=0,new_i=0;
	     old_i < boot_order->data_size / sizeof(*new_data);
	     old_i++) {
		if (old_data[old_i] != num) {
				/* Copy this value */
			new_data[new_i] = old_data[old_i];
			new_i++;
		}
	}

	/* Now new_data has what we need */
	free(boot_order->data);
	boot_order->data = (uint8_t *)new_data;
	boot_order->data_size = new_data_size;
	efi_del_variable(EFI_GLOBAL_GUID, "BootOrder");
	rc = efi_set_variable(EFI_GLOBAL_GUID, "BootOrder", boot_order->data,
				boot_order->data_size, boot_order->attributes);
	free(boot_order->data);
	return rc;
}

static int
read_boot_u16(const char *name)
{
	efi_guid_t guid = EFI_GLOBAL_GUID;
	uint16_t *data = NULL;
	size_t data_size = 0;
	uint32_t attributes = 0;
	int rc;

	rc = efi_get_variable(guid, name, (uint8_t **)&data, &data_size,
				&attributes);
	if (rc < 0)
		return rc;
	if (data_size != 2) {
		if (data != NULL)
			free(data);
		errno = EINVAL;
		return -1;
	}

	rc = data[0];
	free(data);
	return rc;
}

static int
delete_boot_var(uint16_t num)
{
	int rc;
	char name[16];
	list_t *pos, *n;
	efi_variable_t *boot;

	snprintf(name, sizeof(name), "Boot%04X", num);
	rc = efi_del_variable(EFI_GLOBAL_GUID, name);

	/* For backwards compatibility, try to delete abcdef entries as well */
	if (rc < 0) {
		if (errno == ENOENT) {
			snprintf(name, sizeof(name), "Boot%04x", num);
			rc = efi_del_variable(EFI_GLOBAL_GUID, name);
		} else if (errno == EPERM) {
			warn("Could not delete Boot%04X", num);
			return rc;
		}
	}

	if (rc < 0) {
		warnx("Boot entry %04X not found", num);
		return rc;
	}
	list_for_each_safe(pos, n, &boot_entry_list) {
		boot = list_entry(pos, efi_variable_t, list);
		if (boot->num == num) {
			rc = remove_from_boot_order(num);
			if (rc < 0)
				return rc;
			list_del(&(boot->list));
			break; /* short-circuit since it was found */
		}
	}
	return 0;
}

static void
set_var_nums(list_t *list)
{
	list_t *pos;
	efi_variable_t *var;
	int num=0, rc;
	char *name;
	int warn=0;

	list_for_each(pos, list) {
		var = list_entry(pos, efi_variable_t, list);
		rc = sscanf(var->name, "Boot%04X-%*s", &num);
		if (rc == 1) {
			var->num = num;
			name = var->name; /* shorter name */
			if ((isalpha(name[4]) && islower(name[4])) ||
			    (isalpha(name[5]) && islower(name[5])) ||
			    (isalpha(name[6]) && islower(name[6])) ||
			    (isalpha(name[7]) && islower(name[7]))) {
				fprintf(stderr, "** Warning ** : %.8s is not "
				        "EFI 1.10 compliant (lowercase hex in name)\n", name);
				warn++;
			}
		}
	}
	if (warn) {
		fprintf(stderr, "** Warning ** : please recreate these using efibootmgr to remove this warning.\n");
	}
}

static void
unparse_boot_order(uint16_t *order, int length)
{
	int i;
	printf("BootOrder: ");
	for (i=0; i<length; i++) {
		printf("%04X", order[i]);
		if (i < (length-1))
			printf(",");
	}
	printf("\n");
}

static int
is_current_boot_entry(int b)
{
	list_t *pos;
	efi_variable_t *boot;

	list_for_each(pos, &boot_entry_list) {
		boot = list_entry(pos, efi_variable_t, list);
		if (boot->num == b)
			return 1;
	}
	return 0;
}

static void
print_error_arrow(char *message, char *buffer, off_t offset)
{
	unsigned int i;
	fprintf(stderr, "%s: %s\n", message, buffer);
	for (i = 0; i < strlen(message) + 2; i++)
		fprintf(stderr, " ");
	for (i = 0; i < offset; i++)
		fprintf(stderr, " ");
	fprintf(stderr, "^\n");
}

static int
parse_boot_order(char *buffer, uint16_t **order, size_t *length)
{
	uint16_t *data;
	size_t data_size;
	size_t len = strlen(buffer);
	intptr_t end = (intptr_t)buffer + len + 1;

	int num = 0;
	char *buf = buffer;
	while ((intptr_t)buf < end) {
		size_t comma = strcspn(buf, ",");
		if (comma == 0) {
			off_t offset = (intptr_t)buf - (intptr_t)buffer;
			print_error_arrow("Malformed boot order",buffer,offset);
			exit(8);
		} else {
			num++;
		}
		buf += comma + 1;
	}

	data = calloc(num, sizeof (*data));
	if (!data)
		return -1;
	data_size = num * sizeof (*data);

	int i = 0;
	buf = buffer;
	while ((intptr_t)buf < end) {
		unsigned long result = 0;
		size_t comma = strcspn(buf, ",");

		buf[comma] = '\0';
		char *endptr = NULL;
		result = strtoul(buf, &endptr, 16);
		if ((result == ULONG_MAX && errno == ERANGE) ||
				(endptr && *endptr != '\0')) {
			print_error_arrow("Invalid boot order", buffer,
				(intptr_t)endptr - (intptr_t)buffer);
			free(data);
			exit(8);
		}
		if (result > 0xffff) {
			fprintf(stderr, "Invalid boot order entry value: %lX\n",
				result);
			print_error_arrow("Invalid boot order", buffer,
				(intptr_t)buf - (intptr_t)buffer);
			free(data);
			exit(8);
		}

		/* make sure this is an existing boot entry */
		if (!is_current_boot_entry(result)) {
			print_error_arrow("Invalid boot order entry value",
					buffer,
					(intptr_t)buf - (intptr_t)buffer);
			fprintf(stderr,"Boot entry %04lX does not exist\n",
				result);
			free(data);
			exit(8);
		}

		data[i++] = result;
		buf[comma] = ',';
		buf += comma + 1;
	}

	*order = data;
	*length = data_size;
	return num;
}

static int
construct_boot_order(char *bootorder, int keep,
			uint16_t **ret_data, size_t *ret_data_size)
{
	efi_variable_t bo;
	int rc;
	uint16_t *data = NULL;
	size_t data_size = 0;

	rc = parse_boot_order(bootorder, (uint16_t **)&data, &data_size);
	if (rc < 0 || data_size == 0) {
		if (data) /* this can't actually happen, but covscan believes */
			free(data);
		return rc;
	}

	if (!keep) {
		*ret_data = data;
		*ret_data_size = data_size;
		return 0;
	}

	rc = efi_get_variable(EFI_GLOBAL_GUID, "BootOrder",
				&bo.data, &bo.data_size, &bo.attributes);
	if (rc < 0) {
		*ret_data = data;
		*ret_data_size = data_size;
		return 0;
	}

	/* latest apple firmware sets high bit which appears invalid
	 * to the linux kernel if we write it back so lets zero it out
	 * if it is set since it would be invalid to set it anyway */
	bo.attributes = bo.attributes & ~(1 << 31);

	size_t new_data_size = data_size + bo.data_size;
	uint16_t *new_data = calloc(1, new_data_size);
	if (!new_data) {
		if (data)
			free(data);
		return -1;
	}

	memcpy(new_data, data, data_size);
	memcpy(new_data + (data_size / sizeof (*new_data)), bo.data,
			bo.data_size);

	free(bo.data);
	free(data);

	int new_data_start = data_size / sizeof (uint16_t);
	int new_data_end = new_data_size / sizeof (uint16_t);
	int i;
	for (i = 0; i < new_data_start; i++) {
		int j;
		for (j = new_data_start; j < new_data_end; j++) {
			if (new_data[i] == new_data[j]) {
				memcpy(new_data + j, new_data + j + 1,
					sizeof (uint16_t) * (new_data_end-j+1));
				new_data_end -= 1;
				break;
			}
		}
	}
	*ret_data = new_data;
	*ret_data_size = new_data_end * sizeof (uint16_t);
	return 0;
}

static int
set_boot_order(int keep_old_entries)
{
	uint8_t *data = NULL;
	size_t data_size = 0;
	int rc;

	if (!opts.bootorder)
		return 0;

	rc = construct_boot_order(opts.bootorder, keep_old_entries,
				(uint16_t **)&data, &data_size);
	if (rc < 0 || data_size == 0)
		return rc;

	return efi_set_variable(EFI_GLOBAL_GUID, "BootOrder", data, data_size,
				EFI_VARIABLE_NON_VOLATILE |
				EFI_VARIABLE_BOOTSERVICE_ACCESS |
				EFI_VARIABLE_RUNTIME_ACCESS);
}

static void
show_boot_vars()
{
	list_t *pos;
	efi_variable_t *boot;
	char description[80];
	EFI_LOAD_OPTION *load_option;
	EFI_DEVICE_PATH *path;
	unsigned long optional_data_len=0;

	list_for_each(pos, &boot_entry_list) {
		boot = list_entry(pos, efi_variable_t, list);
		load_option = (EFI_LOAD_OPTION *)boot->data;
		efichar_to_char(description,
				load_option->description, sizeof(description));
		path = load_option_path(load_option);
		if (boot->name)
			printf("%.8s", boot->name);
		else
			printf("Boot%04X", boot->num);

		if (load_option->attributes & LOAD_OPTION_ACTIVE)
			printf("* ");
		else    printf("  ");
		printf("%s", description);

		if (opts.verbose) {
			char *text_path = NULL;
			size_t text_path_len = 0;
			ssize_t rc;

			rc = unparse_path(text_path, text_path_len, path,
				     load_option->file_path_list_length);
			if (rc < 0) {
				fprintf(stderr, "Could not parse device path: %m\n");
				exit(1);
			}
			rc += 1;

			text_path_len = rc;
			text_path = calloc(1, rc);
			if (!text_path) {
				fprintf(stderr, "Could not parse device path: %m\n");
				exit(1);
			}
			rc = unparse_path(text_path, text_path_len,
				path, load_option->file_path_list_length);
			if (rc < 0) {
				fprintf(stderr, "Could not parse device path: %m\n");
				exit(1);
			}
			printf("\t%s", text_path);
			free(text_path);
			text_path_len = 0;
			/* Print optional data */

			optional_data_len =
				boot->data_size -
				load_option->file_path_list_length -
				((char *)path - (char *)load_option);
			if (optional_data_len <= 0) {
				printf("\n");
				continue;
			}

			rc = unparse_raw_text(NULL, 0,
				((uint8_t *)path)
					+ load_option->file_path_list_length,
				optional_data_len);
			if (rc < 0) {
				fprintf(stderr, "Could not parse device path: %m\n");
				exit(1);
			}
			rc += 1;
			text_path_len = rc;
			text_path = calloc(1, rc);
			if (!text_path) {
				fprintf(stderr, "Could not parse device path: %m\n");
				exit(1);
			}
			rc = unparse_raw_text(text_path, text_path_len,
				((uint8_t *)path)
					+ load_option->file_path_list_length,
				optional_data_len);
			if (rc < 0) {
				fprintf(stderr, "Could not parse device path: %m\n");
				exit(1);
			}
			printf("%s", text_path);
			free(text_path);
		}
		printf("\n");
	}
}

static void
show_boot_order()
{
	int rc;
	efi_variable_t *boot_order = NULL;
	uint16_t *data;

	rc = read_boot_order(&boot_order);

	if (rc < 0) {
		if (errno == ENOENT) {
			boot_order = calloc(1, sizeof (*boot_order));
			if (!boot_order) {
				perror("show_boot_order()");
				return;
			}
		} else {
			perror("show_boot_order()");
			return;
		}
	}

	/* We've now got an array (in boot_order->data) of the
	   boot order.  First add our entry, then copy the old array.
	*/
	data = (uint16_t *)boot_order->data;
	if (boot_order->data_size) {
		unparse_boot_order(data, boot_order->data_size / sizeof(uint16_t));
		free(boot_order->data);
	}
	free(boot_order);
}

static int
set_active_state()
{
	list_t *pos;
	efi_variable_t *boot;
	EFI_LOAD_OPTION *load_option;

	list_for_each(pos, &boot_entry_list) {
		boot = list_entry(pos, efi_variable_t, list);
		load_option = (EFI_LOAD_OPTION *)boot->data;
		if (boot->num == opts.bootnum) {
			if (opts.active == 1) {
				if (load_option->attributes
						& LOAD_OPTION_ACTIVE) {
					return 0;
				} else {
					load_option->attributes
						|= LOAD_OPTION_ACTIVE;
					return efi_set_variable(boot->guid,
							boot->name,
							boot->data,
							boot->data_size,
							boot->attributes);
				}
			}
			else if (opts.active == 0) {
				if (!(load_option->attributes
						& LOAD_OPTION_ACTIVE)) {
					return 0;
				} else {
					load_option->attributes
						&= ~LOAD_OPTION_ACTIVE;
					return efi_set_variable(boot->guid,
							boot->name,
							boot->data,
							boot->data_size,
							boot->attributes);
				}
			}
		}
	}
	/* if we reach here then the bootnumber supplied was not found */
	fprintf(stderr,"\nboot entry %x not found\n\n",opts.bootnum);
	errno = ENOENT;
	return -1;
}




static void
usage()
{
	printf("efibootmgr version %s\n", EFIBOOTMGR_VERSION);
	printf("usage: efibootmgr [options]\n");
	printf("\t-a | --active         sets bootnum active\n");
	printf("\t-A | --inactive       sets bootnum inactive\n");
	printf("\t-b | --bootnum XXXX   modify BootXXXX (hex)\n");
	printf("\t-B | --delete-bootnum delete bootnum (hex)\n");
	printf("\t-c | --create         create new variable bootnum and add to bootorder\n");
	printf("\t-D | --remove-dups	remove duplicate values from BootOrder\n");
	printf("\t-d | --disk disk       (defaults to /dev/sda) containing loader\n");
	printf("\t-e | --edd [1|3|-1]   force EDD 1.0 or 3.0 creation variables, or guess\n");
	printf("\t-E | --device num      EDD 1.0 device number (defaults to 0x80)\n");
	printf("\t-g | --gpt            force disk with invalid PMBR to be treated as GPT\n");
	printf("\t-H | --acpi_hid XXXX  set the ACPI HID (used with -i)\n");
	printf("\t-i | --iface name     create a netboot entry for the named interface\n");
	printf("\t-l | --loader name     (defaults to \\EFI\\redhat\\grub.efi)\n");
	printf("\t-L | --label label     Boot manager display label (defaults to \"Linux\")\n");
	printf("\t-n | --bootnext XXXX   set BootNext to XXXX (hex)\n");
	printf("\t-N | --delete-bootnext delete BootNext\n");
	printf("\t-o | --bootorder XXXX,YYYY,ZZZZ,...     explicitly set BootOrder (hex)\n");
	printf("\t-O | --delete-bootorder delete BootOrder\n");
	printf("\t-p | --part part        (defaults to 1) containing loader\n");
	printf("\t-q | --quiet            be quiet\n");
	printf("\t-t | --timeout seconds  set boot manager timeout waiting for user input.\n");
	printf("\t-T | --delete-timeout   delete Timeout.\n");
	printf("\t-u | --unicode | --UCS-2  pass extra args as UCS-2 (default is ASCII)\n");
	printf("\t-U | --acpi_uid XXXX    set the ACPI UID (used with -i)\n");
	printf("\t-v | --verbose          print additional information\n");
	printf("\t-V | --version          return version and exit\n");
	printf("\t-w | --write-signature  write unique sig to MBR if needed\n");
	printf("\t-@ | --append-binary-args file  append extra args from file (use \"-\" for stdin)\n");
	printf("\t-h | --help             show help/usage\n");
}

static void
set_default_opts()
{
	memset(&opts, 0, sizeof(opts));
	opts.bootnum         = -1;   /* auto-detect */
	opts.bootnext        = -1;   /* Don't set it */
	opts.active          = -1;   /* Don't set it */
	opts.timeout         = -1;   /* Don't set it */
	opts.edd10_devicenum = 0x80;
	opts.loader          = "\\EFI\\redhat\\grub.efi";
	opts.label           = "Linux";
	opts.disk            = "/dev/sda";
	opts.iface           = NULL;
	opts.part            = 1;
	opts.acpi_hid        = -1;
	opts.acpi_uid        = -1;
}

static void
parse_opts(int argc, char **argv)
{
	int c, rc;
	unsigned int num;
	int option_index = 0;

	while (1)
	{
		static struct option long_options[] =
			/* name, has_arg, flag, val */
		{
			{"active",                 no_argument, 0, 'a'},
			{"inactive",               no_argument, 0, 'A'},
			{"bootnum",          required_argument, 0, 'b'},
			{"delete-bootnum",         no_argument, 0, 'B'},
			{"create",                 no_argument, 0, 'c'},
			{"remove-dups",            no_argument, 0, 'D'},
			{"disk",             required_argument, 0, 'd'},
			{"iface",            required_argument, 0, 'i'},
			{"acpi_hid",         required_argument, 0, 'H'},
			{"edd-device",       required_argument, 0, 'E'},
			{"edd30",            required_argument, 0, 'e'},
			{"gpt",                    no_argument, 0, 'g'},
			{"keep",                   no_argument, 0, 'k'},
			{"loader",           required_argument, 0, 'l'},
			{"label",            required_argument, 0, 'L'},
			{"bootnext",         required_argument, 0, 'n'},
			{"delete-bootnext",        no_argument, 0, 'N'},
			{"bootorder",        required_argument, 0, 'o'},
			{"delete-bootorder",       no_argument, 0, 'O'},
			{"part",             required_argument, 0, 'p'},
			{"quiet",                  no_argument, 0, 'q'},
			{"timeout",          required_argument, 0, 't'},
			{"delete-timeout",         no_argument, 0, 'T'},
			{"unicode",                no_argument, 0, 'u'},
			{"UCS-2",                  no_argument, 0, 'u'},
			{"acpi_uid",         required_argument, 0, 'U'},
			{"verbose",          optional_argument, 0, 'v'},
			{"version",                no_argument, 0, 'V'},
			{"write-signature",        no_argument, 0, 'w'},
			{"append-binary-args", required_argument, 0, '@'},
			{"help",                   no_argument, 0, 'h'},
			{0, 0, 0, 0}
		};

		c = getopt_long (argc, argv,
				 "AaBb:cDd:e:E:gH:i:l:L:n:No:Op:qt:TuU:v::Vw"
				 "@:h",
				 long_options, &option_index);
		if (c == -1)
			break;

		switch (c)
		{
		case '@':
			opts.extra_opts_file = optarg;
			break;
		case 'a':
			opts.active = 1;
			break;
		case 'A':
			opts.active = 0;
			break;
		case 'B':
			opts.delete_boot = 1;
			break;
		case 'b': {
			char *endptr = NULL;
			unsigned long result;
			result = strtoul(optarg, &endptr, 16);
			if ((result == ULONG_MAX && errno == ERANGE) ||
					(endptr && *endptr != '\0')) {
				print_error_arrow("Invalid bootnum value",
					optarg,
					(intptr_t)endptr - (intptr_t)optarg);
				exit(1);
			}
			if (result > 0xffff) {
				fprintf(stderr, "Invalid bootnum value: %lX\n",
					result);
				exit(1);
			}

			opts.bootnum = result;
			break;
		}
		case 'c':
			opts.create = 1;
			break;
		case 'D':
			opts.deduplicate = 1;
			break;
		case 'd':
			opts.disk = optarg;
			break;
		case 'e':
			rc = sscanf(optarg, "%u", &num);
			if (rc == 1) opts.edd_version = num;
			else {
				fprintf (stderr,"invalid numeric value %s\n",optarg);
				exit(1);
			}
			break;
		case 'E':
			rc = sscanf(optarg, "%x", &num);
			if (rc == 1) opts.edd10_devicenum = num;
			else {
				fprintf (stderr,"invalid hex value %s\n",optarg);
				exit(1);
			}
			break;
		case 'g':
			opts.forcegpt = 1;
			break;

		case 'h':
			usage();
			exit(0);
			break;

		case 'H': {
			char *endptr = NULL;
			unsigned long result;
			result = strtoul(optarg, &endptr, 16);
			if ((result == ULONG_MAX && errno == ERANGE) ||
					(endptr && *endptr != '\0')) {
				print_error_arrow("Invalid ACPI_HID value",
					optarg,
					(intptr_t)endptr - (intptr_t)optarg);
				exit(1);
			}
			if (result > 0xffff) {
				fprintf(stderr, "Invalid ACPI_HID value: %lX\n",
					result);
				exit(1);
			}

			opts.acpi_hid = result;
			break;
		}
		case 'i':
			opts.iface = optarg;
			break;
		case 'k':
			opts.keep_old_entries = 1;
			break;
		case 'l':
			opts.loader = optarg;
			break;
		case 'L':
			opts.label = optarg;
			break;
		case 'N':
			opts.delete_bootnext = 1;
			break;
		case 'n': {
			char *endptr = NULL;
			unsigned long result;
			result = strtoul(optarg, &endptr, 16);
			if ((result == ULONG_MAX && errno == ERANGE) ||
					(endptr && *endptr != '\0')) {
				print_error_arrow("Invalid BootNext value",
					optarg,
					(intptr_t)endptr - (intptr_t)optarg);
				exit(1);
			}
			if (result > 0xffff) {
				fprintf(stderr, "Invalid BootNext value: %lX\n",
					result);
				exit(1);
			}
			opts.bootnext = result;
			break;
		}
		case 'o':
			opts.bootorder = optarg;
			break;
		case 'O':
			opts.delete_bootorder = 1;
			break;
		case 'p':
			rc = sscanf(optarg, "%u", &num);
			if (rc == 1) opts.part = num;
			else {
				fprintf (stderr,"invalid numeric value %s\n",optarg);
				exit(1);
			}
			break;
		case 'q':
			opts.quiet = 1;
			break;
		case 't':
			rc = sscanf(optarg, "%u", &num);
			if (rc == 1) {
				opts.timeout = num;
				opts.set_timeout = 1;
			}
			else {
				fprintf (stderr,"invalid numeric value %s\n",optarg);
				exit(1);
			}
			break;
		case 'T':
			opts.delete_timeout = 1;
			break;
		case 'u':
			opts.unicode = 1;
			break;

		case 'U': {
			char *endptr = NULL;
			unsigned long result;
			result = strtoul(optarg, &endptr, 16);
			if ((result == ULONG_MAX && errno == ERANGE) ||
					(endptr && *endptr != '\0')) {
				print_error_arrow("Invalid ACPI_UID value",
					optarg,
					(intptr_t)endptr - (intptr_t)optarg);
				exit(1);
			}
			if (result > 0xffff) {
				fprintf(stderr, "Invalid ACPI_UID value: %lX\n",
					result);
				exit(1);
			}

			opts.acpi_uid = result;
			break;
		}
		case 'v':
			opts.verbose = 1;
			if (optarg) {
				if (!strcmp(optarg, "v"))  opts.verbose = 2;
				if (!strcmp(optarg, "vv")) opts.verbose = 3;
				rc = sscanf(optarg, "%u", &num);
				if (rc == 1)  opts.verbose = num;
				else {
					fprintf (stderr,"invalid numeric value %s\n",optarg);
					exit(1);
				}
			}
			break;
		case 'V':
			opts.showversion = 1;
			break;

		case 'w':
			opts.write_signature = 1;
			break;

		default:
			usage();
			exit(1);
		}
	}

	if (optind < argc) {
		opts.argc = argc;
		opts.argv = argv;
		opts.optind = optind;
	}
}


int
main(int argc, char **argv)
{
	char **boot_names = NULL;
	efi_variable_t *new_boot = NULL;
	int num;
	int ret = 0;

	set_default_opts();
	parse_opts(argc, argv);
	if (opts.showversion) {
		printf("version %s\n", EFIBOOTMGR_VERSION);
		return 0;
	}

	if (opts.iface && (
			opts.acpi_hid < 0 || opts.acpi_uid < 0 ||
			opts.acpi_hid > UINT32_MAX ||
			opts.acpi_uid > UINT32_MAX))
		errx(1, "You must specify the ACPI HID and UID when using -i.");

	if (!efi_variables_supported())
		errx(2, "EFI variables are not supported on this system.");

	read_boot_var_names(&boot_names);
	read_vars(boot_names, &boot_entry_list);
	set_var_nums(&boot_entry_list);

	if (opts.delete_boot) {
		if (opts.bootnum == -1)
			errx(3, "You must specify a boot entry to delete "
				"(see the -b option).");
		else {
			ret = delete_boot_var(opts.bootnum);
			if (ret < 0)
				err(15, "Could not delete boot variable");
		}
	}

	if (opts.active >= 0) {
		if (opts.bootnum == -1)
			errx(4, "You must specify a boot entry to activate "
				"(see the -b option");
		else {
			ret = set_active_state();
			if (ret < 0)
				err(16, "Could not set active state");
		}
	}

	if (opts.create) {
		warn_duplicate_name(&boot_entry_list);
		new_boot = make_boot_var(&boot_entry_list);
		if (!new_boot)
			err(5, "Could not prepare boot variable");

		/* Put this boot var in the right BootOrder */
		if (new_boot)
			ret=add_to_boot_order(new_boot->num);
		if (ret < 0)
			err(6, "Could not add entry to BootOrder");
	}

	if (opts.delete_bootorder) {
		ret = efi_del_variable(EFI_GLOBAL_GUID, "BootOrder");
		if (ret < 0)
			err(7, "Could not remove entry from BootOrder");
	}

	if (opts.bootorder) {
		ret = set_boot_order(opts.keep_old_entries);
		if (ret < 0)
			err(8, "Could not set BootOrder");
	}

	if (opts.deduplicate) {
		ret = remove_dupes_from_boot_order();
		if (ret)
			err(9, "Could not set BootOrder");
	}

	if (opts.delete_bootnext) {
		if (!is_current_boot_entry(opts.delete_bootnext))
			errx(17, "Boot entry %04X does not exist\n",
				opts.delete_bootnext);

		ret = efi_del_variable(EFI_GLOBAL_GUID, "BootNext");
		if (ret < 0)
			err(10, "Could not delete BootNext");
	}

	if (opts.delete_timeout) {
		ret = efi_del_variable(EFI_GLOBAL_GUID, "Timeout");
		if (ret < 0)
			err(11, "Could not delete Timeout");
	}

	if (opts.bootnext >= 0) {
		if (!is_current_boot_entry(opts.bootnext & 0xFFFF))
			errx(12, "Boot entry %X does not exist", opts.bootnext);
		ret = set_boot_u16("BootNext", opts.bootnext & 0xFFFF);
		if (ret < 0)
			err(13, "Could not set BootNext");
	}

	if (opts.set_timeout) {
		ret = set_boot_u16("Timeout", opts.timeout);
		if (ret < 0)
			err(14, "Could not set Timeout");
	}

	if (!opts.quiet && ret == 0) {
		num = read_boot_u16("BootNext");
		if (num >= 0) {
			printf("BootNext: %04X\n", num);
		}
		num = read_boot_u16("BootCurrent");
		if (num >= 0) {
			printf("BootCurrent: %04X\n", num);
		}
		num = read_boot_u16("Timeout");
		if (num >= 0) {
			printf("Timeout: %u seconds\n", num);
		}
		show_boot_order();
		show_boot_vars();
	}
	free_vars(&boot_entry_list);
	free_array(boot_names);
	if (ret)
		return 1;
	return 0;
}

