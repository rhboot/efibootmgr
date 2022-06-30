/*
  efibootmgr.c - Manipulates EFI variables as exported in /sys/firmware/efi/
    efivars or vars (previously /proc/efi/vars)

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


  This must tie the EFI_DEVICE_PATH to /boot/efi/EFI/<vendor>/<loader>.efi
  The  EFI_DEVICE_PATH will look something like:
    ACPI device path, length 12 bytes
    Hardware Device Path, PCI, length 6 bytes
    Messaging Device Path, SCSI, length 8 bytes, or ATAPI, length ??
    Media Device Path, Hard Drive, partition XX, length 30 bytes
    Media Device Path, File Path, length ??
    End of Hardware Device Path, length 4
    Arguments passed to elilo, as UCS-2 characters, length ??

*/

#include "fix_coverity.h"

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
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
#include <efiboot.h>
#include <inttypes.h>

#include "list.h"
#include "efi.h"
#include "parse_loader_data.h"
#include "efibootmgr.h"
#include "error.h"

#ifndef EFIBOOTMGR_VERSION
#define EFIBOOTMGR_VERSION "unknown (fix Makefile!)"
#endif

int verbose;

typedef struct _var_entry {
	char		*name;
	efi_guid_t	guid;
	uint8_t		*data;
	size_t		data_size;
	uint32_t	attributes;
	uint16_t	num;
	list_t		list;
} var_entry_t;

/* global variables */
static	LIST_HEAD(entry_list);
static	LIST_HEAD(blk_list);
efibootmgr_opt_t opts;

static void
free_vars(list_t *head)
{
	list_t *pos, *n;
	var_entry_t *entry;

	list_for_each_safe(pos, n, head) {
		entry = list_entry(pos, var_entry_t, list);
		if (entry->name)
			free(entry->name);
		if (entry->data)
			free(entry->data);
		list_del(&(entry->list));
		free(entry);
	}
}

static void
read_vars(char **namelist,
	  list_t *head)
{
	var_entry_t *entry;
	int i, rc;

	if (!namelist)
		return;

	for (i=0; namelist[i] != NULL; i++) {
		if (namelist[i]) {
			entry = calloc(1, sizeof(var_entry_t));
			if (!entry) {
				efi_error("calloc(1, %zd) failed",
					  sizeof(var_entry_t));
				goto err;
			}

			rc = efi_get_variable(EFI_GLOBAL_GUID, namelist[i],
					       &entry->data, &entry->data_size,
					       &entry->attributes);
			if (rc < 0) {
				warning("Skipping unreadable variable \"%s\"",
					namelist[i]);
				free(entry);
				continue;
			}

			/* latest apple firmware sets high bit which appears
			 * invalid to the linux kernel if we write it back so
			 * lets zero it out if it is set since it would be
			 * invalid to set it anyway */
			entry->attributes = entry->attributes & ~(1 << 31);

			entry->name = strdup(namelist[i]);
			if (!entry->name) {
				efi_error("strdup(\"%s\") failed", namelist[i]);
				goto err;
			}
			entry->guid = EFI_GLOBAL_GUID;
			list_add_tail(&entry->list, head);
		}
	}
	return;
err:
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
  Return an available variable number,
  or -1 on failure.
*/
static int
find_free_var(list_t *var_list)
{
	int num_vars=0, i=0, found;
	uint16_t *vars, free_number;
	list_t *pos;
	var_entry_t *entry;

	list_for_each(pos, var_list)
		num_vars++;

	if (num_vars == 0)
		return 0;

	vars = calloc(1, sizeof(uint16_t) * num_vars);
	if (!vars) {
		efi_error("calloc(1, %zd) failed", sizeof(uint16_t) * num_vars);
		return -1;
	}

	list_for_each(pos, var_list) {
		entry = list_entry(pos, var_entry_t, list);
		vars[i] = entry->num;
		i++;
	}
	qsort(vars, i, sizeof(uint16_t), compare);
	found = 1;

	num_vars = i;
	for (free_number = 0; free_number < num_vars && found; free_number++) {
		found = 0;
		list_for_each(pos, var_list) {
			entry = list_entry(pos, var_entry_t, list);
			if (entry->num == free_number) {
				found = 1;
				break;
			}
		}
		if (!found)
			break;
	}
	if (found && num_vars)
		free_number = vars[num_vars-1] + 1;
	free(vars);
	return free_number;
}


static void
warn_duplicate_name(list_t *var_list)
{
	list_t *pos;
	var_entry_t *entry;
	efi_load_option *load_option;
	const unsigned char *desc;

	list_for_each(pos, var_list) {
		entry = list_entry(pos, var_entry_t, list);
		load_option = (efi_load_option *)entry->data;
		desc = efi_loadopt_desc(load_option, entry->data_size);
		if (!strcmp((char *)opts.label, (char *)desc))
			warnx("** Warning ** : %s has same label %s",
			      entry->name, opts.label);
	}
}

static var_entry_t *
make_var(const char *prefix, list_t *var_list)
{
	var_entry_t *entry = NULL;
	int free_number;
	list_t *pos;
	int rc;
	uint8_t *extra_args = NULL;
	ssize_t extra_args_size = 0;
	ssize_t needed=0, sz;

	if (opts.num == -1) {
		free_number = find_free_var(var_list);
	} else {
		list_for_each(pos, var_list) {
			entry = list_entry(pos, var_entry_t, list);
			if (entry->num == opts.num)
				errx(40,
				     "Cannot create %s%04X: already exists.",
				     prefix, opts.num);
		}
		free_number = opts.num;
	}

	if (free_number == -1) {
		efi_error("efibootmgr: no available %s variables", prefix);
		return NULL;
	}

	/* Create a new var_entry_t object
	   and populate it.
	*/
	entry = calloc(1, sizeof(*entry));
	if (!entry) {
		efi_error("calloc(1, %zd) failed", sizeof(*entry));
		return NULL;
	}

	sz = get_extra_args(NULL, 0);
	if (sz < 0) {
		efi_error("get_extra_args() failed");
		goto err;
	}
	extra_args_size = sz;

	entry->data = NULL;
	entry->data_size = 0;
	needed = make_linux_load_option(&entry->data, &entry->data_size,
					NULL, sz);
	if (needed < 0) {
		efi_error("make_linux_load_option() failed");
		goto err;
	}
	entry->data_size = needed;
	entry->data = malloc(needed);
	if (!entry->data) {
		efi_error("malloc(%zd) failed", needed);
		goto err;
	}

	extra_args = entry->data + needed - extra_args_size;
	sz = get_extra_args(extra_args, extra_args_size);
	if (sz < 0) {
		efi_error("get_extra_args() failed");
		goto err;
	}
	sz = make_linux_load_option(&entry->data, &entry->data_size,
				    extra_args, extra_args_size);
	if (sz < 0) {
		efi_error("make_linux_load_option failed");
		goto err;
	}

	entry->num = free_number;
	entry->guid = EFI_GLOBAL_GUID;
	rc = asprintf(&entry->name, "%s%04X", prefix, free_number);
	if (rc < 0) {
		efi_error("asprintf failed");
		goto err;
	}
	entry->attributes = EFI_VARIABLE_NON_VOLATILE |
			    EFI_VARIABLE_BOOTSERVICE_ACCESS |
			    EFI_VARIABLE_RUNTIME_ACCESS;
	rc = efi_set_variable(entry->guid, entry->name, entry->data,
				entry->data_size, entry->attributes, 0644);
	if (rc < 0) {
		efi_error("efi_set_variable failed");
		goto err;
	}
	list_add_tail(&entry->list, var_list);
	return entry;
err:
	if (entry) {
		if (entry->data)
			free(entry->data);
		if (entry->name) {
			efi_error("Could not set variable %s", entry->name);
			free(entry->name);
		} else {
			efi_error("Could not set variable");
		}
		free(entry);
	}
	return NULL;
}

static int
read_order(const char *name, var_entry_t **order)
{
	int rc;
	var_entry_t *new = NULL, *bo = NULL;

	if (*order == NULL) {
		new = calloc(1, sizeof (**order));
		if (!new) {
			efi_error("calloc(1, %zd) failed",
				  sizeof (**order));
			return -1;
		}
		*order = bo = new;
	} else {
		bo = *order;
	}

	rc = efi_get_variable(EFI_GLOBAL_GUID, name,
				&bo->data, &bo->data_size, &bo->attributes);
	if (rc < 0 && new != NULL) {
		efi_error("efi_get_variable failed");
		free(new);
		*order = NULL;
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
set_u16(const char *name, uint16_t num)
{
	return efi_set_variable(EFI_GLOBAL_GUID, name, (uint8_t *)&num,
				sizeof (num), EFI_VARIABLE_NON_VOLATILE |
					      EFI_VARIABLE_BOOTSERVICE_ACCESS |
					      EFI_VARIABLE_RUNTIME_ACCESS,
					      0644);
}

static int
add_to_order(const char *name, uint16_t num, uint16_t insert_at)
{
	var_entry_t *order = NULL;
	uint64_t new_data_size;
	uint16_t *new_data, *old_data;
	int rc;

	rc = read_order(name, &order);
	if (rc < 0) {
		if (errno == ENOENT)
			rc = set_u16(name, num);
		return rc;
	}

	/* We've now got an array (in order->data) of the order.  Copy over
	 * any entries that should precede, add our entry, and then copy the
	 * rest of the old array.
	 */
	old_data = (uint16_t *)order->data;
	new_data_size = order->data_size + sizeof(uint16_t);
	new_data = malloc(new_data_size);
	if (!new_data)
		return -1;

	if (insert_at != 0) {
		if (insert_at > order->data_size)
			insert_at = order->data_size;
		memcpy(new_data, old_data, insert_at * sizeof(uint16_t));
	}
	new_data[insert_at] = num;
	if (order->data_size - insert_at * sizeof(uint16_t) > 0) {
		memcpy(new_data + insert_at + 1, old_data + insert_at,
		       order->data_size - insert_at * sizeof(uint16_t));
	}

	/* Now new_data has what we need */
	free(order->data);
	order->data = (uint8_t *)new_data;
	order->data_size = new_data_size;

	rc = efi_set_variable(EFI_GLOBAL_GUID, name, order->data,
			order->data_size, order->attributes, 0644);
	free(order->data);
	free(order);
	return rc;
}

static int
remove_dupes_from_order(char *name)
{
	var_entry_t *order = NULL;
	uint64_t new_data_size;
	uint16_t *new_data, *old_data;
	unsigned int old_i,new_i;
	int rc;

	rc = read_order(name, &order);
	if (rc < 0) {
		if (errno == ENOENT)
			rc = 0;
		return rc;
	}

	old_data = (uint16_t *)(order->data);
	/* Start with the same size */
	new_data_size = order->data_size;
	new_data = malloc(new_data_size);
	if (!new_data)
		return -1;

	unsigned int old_max = order->data_size / sizeof(*new_data);
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
	free(order->data);
	order->data = (uint8_t *)new_data;
	order->data_size = new_data_size;
	efi_del_variable(EFI_GLOBAL_GUID, name);
	rc = efi_set_variable(EFI_GLOBAL_GUID, name, order->data,
				order->data_size, order->attributes,
				0644);
	free(order->data);
	free(order);
	return rc;
}

static int
remove_from_order(const char *name, uint16_t num)
{
	var_entry_t *order = NULL;
	uint16_t *data;
	unsigned int old_i,new_i;
	int rc;

	rc = read_order(name, &order);
	if (rc < 0) {
		if (errno == ENOENT)
			rc = 0;
		return rc;
	}

	/* We've now got an array (in order->data) of the
	   order. Squeeze out any instance of the entry we're
	   deleting by shifting the remainder down.
	*/
	data = (uint16_t *)(order->data);

	for (old_i=0,new_i=0;
	     old_i < order->data_size / sizeof(data[0]);
	     old_i++) {
		if (data[old_i] != num) {
			if (new_i != old_i)
				data[new_i] = data[old_i];
			new_i++;
		}
	}

	/* If nothing removed, no need to update the order variable */
	if (new_i == old_i)
		goto all_done;

	/* *Order should have nothing when new_i == 0 */
	if (new_i == 0) {
		efi_del_variable(EFI_GLOBAL_GUID, name);
		goto all_done;
	}

	order->data_size = sizeof(data[0]) * new_i;
	rc = efi_set_variable(EFI_GLOBAL_GUID, name, order->data,
				order->data_size, order->attributes,
				0644);
all_done:
	free(order->data);
	free(order);
	return rc;
}

static int
read_u16(const char *name)
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
hex_could_be_lower_case(uint16_t num)
{
	return ((((num & 0x000f) >>  0) > 9) ||
		(((num & 0x00f0) >>  4) > 9) ||
		(((num & 0x0f00) >>  8) > 9) ||
		(((num & 0xf000) >> 12) > 9));
}

static int
delete_var(const char *prefix, uint16_t num)
{
	int rc;
	char name[16];
	list_t *pos, *n;
	var_entry_t *entry;

	snprintf(name, sizeof(name), "%s%04X", prefix, num);
	rc = efi_del_variable(EFI_GLOBAL_GUID, name);
	if (rc < 0)
		efi_error("Could not delete %s%04X", prefix, num);

	/* For backwards compatibility, try to delete abcdef entries as well */
	if (rc < 0 && errno == ENOENT && hex_could_be_lower_case(num)) {
		snprintf(name, sizeof(name), "%s%04x", prefix, num);
		rc = efi_del_variable(EFI_GLOBAL_GUID, name);
		if (rc < 0 && errno != ENOENT)
			efi_error("Could not delete %s%04x", prefix, num);
	}

	if (rc < 0)
		return rc;
	else
		efi_error_clear();

	snprintf(name, sizeof(name), "%sOrder", prefix);

	list_for_each_safe(pos, n, &entry_list) {
		entry = list_entry(pos, var_entry_t, list);
		if (entry->num == num) {
			rc = remove_from_order(name, num);
			if (rc < 0) {
				efi_error("remove_from_order(%s,%d) failed",
					  name, num);
				return rc;
			}
			list_del(&(entry->list));
			free(entry->name);
			free(entry->data);
			memset(entry, 0, sizeof(*entry));
			free(entry);
			break; /* short-circuit since it was found */
		}
	}
	return 0;
}

static int
delete_label(const char *prefix, const unsigned char *label)
{
	list_t *pos;
	var_entry_t *boot;
	int num_deleted = 0;
	int rc;
	efi_load_option *load_option;
	const unsigned char *desc;

	list_for_each(pos, &entry_list) {
		boot = list_entry(pos, var_entry_t, list);
		load_option = (efi_load_option *)boot->data;
		desc = efi_loadopt_desc(load_option, boot->data_size);

		if (strcmp((char *)desc, (char *)label) == 0) {
			rc = delete_var(prefix,boot->num);
			if (rc < 0) {
				efi_error("Could not delete %s%04x", prefix, boot->num);
				return rc;
			} else {
				num_deleted++;
			}
		}
	}

	if (num_deleted == 0) {
		efi_error("Could not delete %s", label);
		return -1;
	}

	return 0;
}

static void
set_var_nums(const char *prefix, list_t *list)
{
	list_t *pos;
	var_entry_t *var;
	int num=0, rc;
	char *name;
	int warn=0;
	size_t plen = strlen(prefix);
	char fmt[30];

	fmt[0] = '\0';
	strcat(fmt, prefix);
	strcat(fmt, "%04X-%*s");

	list_for_each(pos, list) {
		var = list_entry(pos, var_entry_t, list);
		rc = sscanf(var->name, fmt, &num);
		if (rc == 1) {
			char *snum;
			var->num = num;
			name = var->name; /* shorter name */
			snum = name + plen;
			if ((isalpha(snum[0]) && islower(snum[0])) ||
			    (isalpha(snum[1]) && islower(snum[1])) ||
			    (isalpha(snum[2]) && islower(snum[2])) ||
			    (isalpha(snum[3]) && islower(snum[3]))) {
				fprintf(stderr,
					"** Warning ** : %.8s is not UEFI Spec compliant (lowercase hex in name)\n",
					name);
				warn++;
			}
		}
	}
	if (warn)
		warningx("** Warning ** : please recreate these using efibootmgr to remove this warning.");
}

static void
print_order(const char *name, uint16_t *order, int length)
{
	int i;
	printf("%s: ", name);
	for (i=0; i<length; i++) {
		printf("%04X", order[i]);
		if (i < (length-1))
			printf(",");
	}
	printf("\n");
}

static int
is_current_entry(int b)
{
	list_t *pos;
	var_entry_t *entry;

	list_for_each(pos, &entry_list) {
		entry = list_entry(pos, var_entry_t, list);
		if (entry->num == b)
			return 1;
	}
	return 0;
}

static void
print_error_arrow(char *buffer, off_t offset, char *fmt, ...)
{
	va_list ap;
	size_t size;
	unsigned int i;

	va_start(ap, fmt);
	size = vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "%s\n", buffer);

	for (i = 0; i < size + 2; i++)
		fprintf(stderr, " ");
	for (i = 0; i < offset; i++)
		fprintf(stderr, " ");
	fprintf(stderr, "^\n");
}

static int
parse_order(const char *prefix, char *buffer, uint16_t **order, size_t *length)
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
			print_error_arrow(buffer, offset, "Malformed %s order",
					  prefix);
			exit(8);
		} else {
			num++;
		}
		buf += comma + 1;
	}

	if (num == 0) {
		*order = NULL;
		*length = 0;
		return 0;
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
			off_t offset = (intptr_t)endptr - (intptr_t)buffer;
			print_error_arrow(buffer, offset, "Invalid %s order",
					  prefix);
			free(data);
			exit(8);
		}
		if (result > 0xffff) {
			off_t offset = (intptr_t)buf - (intptr_t)buffer;
			warnx("Invalid %s order entry value: %lX", prefix,
				result);
			print_error_arrow(buffer, offset, "Invalid %s order",
					  prefix);
			free(data);
			exit(8);
		}

		/* make sure this is an existing entry */
		if (!is_current_entry(result)) {
			off_t offset = (intptr_t)buf - (intptr_t)buffer;
			print_error_arrow(buffer, offset,
					  "Invalid %s order entry value",
					  prefix);
			warnx("entry %04lX does not exist", result);
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
construct_order(const char *name, char *order, int keep,
			uint16_t **ret_data, size_t *ret_data_size)
{
	var_entry_t bo;
	int rc;
	uint16_t *data = NULL;
	size_t data_size = 0;

	rc = parse_order(name, order, (uint16_t **)&data, &data_size);
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

	rc = efi_get_variable(EFI_GLOBAL_GUID, name,
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
set_order(const char *order_name, const char *prefix, int keep_old_entries)
{
	uint8_t *data = NULL;
	size_t data_size = 0;
	char *name;
	int rc;

	if (!opts.order)
		return 0;

	rc = construct_order(order_name, opts.order, keep_old_entries,
				(uint16_t **)&data, &data_size);
	if (rc < 0 || data_size == 0) {
		if (data) /* this can't happen, but clang analyzer believes */
			free(data);
		return rc;
	}

	rc = asprintf(&name, "%sOrder", prefix);
	if (rc < 0)
		goto err;

	rc = efi_set_variable(EFI_GLOBAL_GUID, name, data, data_size,
			      EFI_VARIABLE_NON_VOLATILE |
			      EFI_VARIABLE_BOOTSERVICE_ACCESS |
			      EFI_VARIABLE_RUNTIME_ACCESS,
			      0644);
	free(name);
err:
	free(data);
	return rc;
}

#define ev_bits(val, mask, shift) \
	(((val) & ((mask) << (shift))) >> (shift))

static inline char *
ucs2_to_utf8(const uint16_t * const chars, ssize_t limit)
{
	ssize_t i, j;
	char *ret;

	ret = alloca(limit * 6 + 1);
	if (!ret)
		return NULL;
	memset(ret, 0, limit * 6 +1);

	for (i=0, j=0; i < (limit >= 0 ? limit : i+1) && chars[i]; i++,j++) {
		if (chars[i] <= 0x7f) {
			ret[j] = chars[i];
		} else if (chars[i] > 0x7f && chars[i] <= 0x7ff) {
			ret[j++] = 0xc0 | ev_bits(chars[i], 0x1f, 6);
			ret[j]   = 0x80 | ev_bits(chars[i], 0x3f, 0);
		} else if (chars[i] > 0x7ff) {
			ret[j++] = 0xe0 | ev_bits(chars[i], 0xf, 12);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 6);
			ret[j]   = 0x80| ev_bits(chars[i], 0x3f, 0);
		}
	}
	ret[j] = '\0';
	return strdup(ret);
}

static void
show_var_path(efi_load_option *load_option, size_t boot_data_size)
{
	char *text_path = NULL;
	size_t text_path_len = 0;
	uint16_t pathlen;
	ssize_t rc;
	efidp dp = NULL;
	unsigned char *optional_data = NULL;
	size_t optional_data_len=0;
	bool is_shim = false;
	const char * const shim_path_segments[] = {
		"/File(\\EFI\\", "\\shim", ".efi)", NULL
	};

	pathlen = efi_loadopt_pathlen(load_option,
				      boot_data_size);
	dp = efi_loadopt_path(load_option, boot_data_size);
	rc = efidp_format_device_path((unsigned char *)text_path,
				      text_path_len, dp, pathlen);
	if (rc < 0) {
		warning("Could not parse device path");
		return;
	}
	rc += 1;

	text_path_len = rc;
	text_path = calloc(1, rc);
	if (!text_path) {
		warning("Could not parse device path");
		return;
	}

	rc = efidp_format_device_path((unsigned char *)text_path,
				      text_path_len, dp, pathlen);
	if (rc >= 0) {
		printf("\t%s", text_path);

		char *a = text_path;
		for (int i = 0; a && shim_path_segments[i] != NULL; i++) {
			a = strstr(a, shim_path_segments[i]);
			if (a)
				a += strlen(shim_path_segments[i]);
		}
		if (a && a[0] == '\0')
			is_shim = true;
	}

	free(text_path);
	if (rc < 0) {
		warning("Could not parse device path");
		return;
	}

	/* Print optional data */
	rc = efi_loadopt_optional_data(load_option, boot_data_size,
				       &optional_data, &optional_data_len);
	if (rc < 0) {
		warning("Could not parse optional data");
		return;
	}

	typedef ssize_t (*parser_t)(char *buffer, size_t buffer_size,
				    uint8_t *p, uint64_t length);
	parser_t parser = NULL;
	if (is_shim && optional_data_len) {
		char *a = ucs2_to_utf8((uint16_t*)optional_data,
				       optional_data_len/2);
		if (!a) {
			warning("Could not parse optional data");
			return;
		}
		text_path = calloc(1, sizeof(" File(.")
				      + strlen(a)
				      + strlen(")"));
		if (!text_path) {
			free(a);
			warning("Could not parse optional data");
			return;
		}
		char *b;

		b = stpcpy(text_path, " File(.");
		b = stpcpy(b, a);
		stpcpy(b, ")");
		free(a);
	} else if (opts.unicode) {
		text_path = ucs2_to_utf8((uint16_t*)optional_data,
					 optional_data_len/2);
		if (!text_path) {
			warning("Could not parse optional data");
			return;
		}
	} else if (optional_data_len == sizeof(efi_guid_t)) {
		parser = parse_efi_guid;
	} else {
		parser = parse_raw_text;
	}

	if (parser) {
		rc = parser(NULL, 0, optional_data, optional_data_len);
		if (rc < 0) {
			warning("Could not parse optional data");
			return;
		}
		rc += 1;
		text_path_len = rc;
		text_path = calloc(1, rc);
		if (!text_path) {
			warning("Could not parse optional data");
			return;
		}
		rc = parser(text_path, text_path_len,
			    optional_data, optional_data_len);
		if (rc < 0) {
			warning("Could not parse device path");
			free(text_path);
			return;
		}
	}
	printf("%s", text_path);
	free(text_path);
	printf("\n");

	const_efidp node = dp;
	if (opts.verbose >= 1)
		printf("      dp: ");
	for (rc = 1; opts.verbose >= 1 && rc > 0; ) {
		ssize_t sz;
		const_efidp next = NULL;
		const uint8_t * const data = (const uint8_t * const)node;

		rc = efidp_next_node(node, &next);
		if (rc < 0) {
			warning("Could not iterate device path");
			return;
		}

		sz = efidp_node_size(node);
		if (sz <= 0) {
			warning("Could not iterate device path");
			return;
		}

		for (ssize_t j = 0; j < sz; j++)
			printf("%02hhx%s", data[j], j == sz - 1 ? "" : " ");
		printf("%s", rc == 0 ? "\n" : " / ");

		node = next;
	}
	if (opts.verbose >= 1 && optional_data_len)
		printf("    data: ");
	for (unsigned int j = 0; opts.verbose >= 1 && j < optional_data_len; j++)
		printf("%02hhx%s", optional_data[j], j == optional_data_len - 1 ? "\n" : " ");
}

static void
show_vars(const char *prefix)
{
	list_t *pos;
	var_entry_t *boot;
	const unsigned char *description;
	efi_load_option *load_option;

	list_for_each(pos, &entry_list) {
		boot = list_entry(pos, var_entry_t, list);
		load_option = (efi_load_option *)boot->data;
		description = efi_loadopt_desc(load_option, boot->data_size);
		if (boot->name)
			printf("%s", boot->name);
		else
			printf("%s%04X", prefix, boot->num);

		printf("%c ", (efi_loadopt_attrs(load_option)
			       & LOAD_OPTION_ACTIVE) ? '*' : ' ');
		printf("%s", description);

		show_var_path(load_option, boot->data_size);

		fflush(stdout);
	}
}

static void
show_order(const char *name)
{
	int rc;
	var_entry_t *order = NULL;
	uint16_t *data;

	rc = read_order(name, &order);
	cond_warning(opts.verbose >= 2 && rc < 0,
		  "Could not read variable '%s'", name);

	if (rc < 0) {
		if (errno == ENOENT) {
			if (!strcmp(name, "BootOrder"))
				printf("No BootOrder is set; firmware will attempt recovery\n");
			else
				printf("No %s is set\n", name);
		} else
			perror("show_order()");
		return;
	}

	/* We've now got an array (in order->data) of the order.  First add
	 * our entry, then copy the old array.
	 */
	data = (uint16_t *)order->data;
	if (order->data_size) {
		print_order(name, data,
				 order->data_size / sizeof(uint16_t));
		free(order->data);
	}
	free(order);
}

static var_entry_t *
get_entry(list_t *entries, uint16_t num)
{
	list_t *pos;
	var_entry_t *entry = NULL;

	list_for_each(pos, entries) {
		entry = list_entry(pos, var_entry_t, list);
		if (entry->num != num) {
			entry = NULL;
			continue;
		}
	}

	return entry;
}

static int
update_entry_attr(var_entry_t *entry, uint64_t attr, bool set)
{
	efi_load_option *load_option;
	uint64_t attrs;
	int rc;

	load_option = (efi_load_option *)entry->data;
	attrs = efi_loadopt_attrs(load_option);

	if ((set && (attrs & attr)) || (!set && !(attrs & attr)))
		return 0;

	if (set)
		efi_loadopt_attr_set(load_option, attr);
	else
		efi_loadopt_attr_clear(load_option, attr);

	rc = efi_set_variable(entry->guid, entry->name,
			      entry->data, entry->data_size,
			      entry->attributes, 0644);
	if (rc < 0) {
		char *guid = NULL;
		int err = errno;

		efi_guid_to_str(&entry->guid, &guid);
		errno = err;
		efi_error("efi_set_variable(%s,%s,...)",
			  guid, entry->name);
	}

	return rc;
}

static int
set_active_state(const char *prefix)
{
	var_entry_t *entry;

	entry = get_entry(&entry_list, opts.num);
	if (!entry) {
		/* if we reach here then the number supplied was not found */
		warnx("%s entry %x not found", prefix, opts.num);
		errno = ENOENT;
		return -1;
	}

	return update_entry_attr(entry, LOAD_OPTION_ACTIVE, opts.active);
}

static int
set_force_reconnect(const char *prefix)
{
	var_entry_t *entry;

	entry = get_entry(&entry_list, opts.num);
	if (!entry) {
		/* if we reach here then the number supplied was not found */
		warnx("%s entry %x not found", prefix, opts.num);
		errno = ENOENT;
		return -1;
	}

	return update_entry_attr(entry, LOAD_OPTION_FORCE_RECONNECT,
				 opts.reconnect > 0);
}

static int
get_mirror(int which, int *below4g, int *above4g, int *mirrorstatus)
{
	int rc;
	uint8_t *data;
	ADDRESS_RANGE_MIRROR_VARIABLE_DATA *abm;
	size_t data_size;
	uint32_t attributes;
	char *name;

	if (which)
		name = ADDRESS_RANGE_MIRROR_VARIABLE_REQUEST;
	else
		name = ADDRESS_RANGE_MIRROR_VARIABLE_CURRENT;

	rc = efi_get_variable(ADDRESS_RANGE_MIRROR_VARIABLE_GUID, name,
				&data, &data_size, &attributes);
	if (rc == 0) {
		abm = (ADDRESS_RANGE_MIRROR_VARIABLE_DATA *)data;
		if (!which && abm->mirror_version != MIRROR_VERSION) {
			rc = 2;
		}
		*below4g = abm->mirror_memory_below_4gb;
		*above4g = abm->mirror_amount_above_4gb;
		*mirrorstatus = abm->mirror_status;
		free(data);
	} else {
		cond_warning(opts.verbose >= 2,
			     "Could not read variable '%s'", name);
		errno = 0;
	}
	return rc;
}

static int
set_mirror(int below4g, int above4g)
{
	int s, status, rc;
	uint8_t *data;
	ADDRESS_RANGE_MIRROR_VARIABLE_DATA abm;
	size_t data_size;
	uint32_t attributes;
	int oldbelow4g, oldabove4g;

	if ((s = get_mirror(0, &oldbelow4g, &oldabove4g, &status)) != 0) {
		if (s == 2)
			warningx("** Warning ** : unrecognised version for memory mirror i/f");
		else
			warningx("** Warning ** : platform does not support memory mirror");
		return s;
	}

	below4g = opts.set_mirror_lo ? below4g : oldbelow4g;
	above4g = opts.set_mirror_hi ? above4g : oldabove4g;
	if (oldbelow4g == below4g && oldabove4g == above4g)
		return 0;

	data = (uint8_t *)&abm;
	data_size = sizeof (abm);
	attributes = EFI_VARIABLE_NON_VOLATILE
		| EFI_VARIABLE_BOOTSERVICE_ACCESS
		| EFI_VARIABLE_RUNTIME_ACCESS;

	abm.mirror_version = MIRROR_VERSION;
	abm.mirror_amount_above_4gb = above4g;
	abm.mirror_memory_below_4gb = below4g;
	abm.mirror_status = 0;
	rc = efi_set_variable(ADDRESS_RANGE_MIRROR_VARIABLE_GUID,
			      ADDRESS_RANGE_MIRROR_VARIABLE_REQUEST, data,
			      data_size, attributes, 0644);
	if (rc < 0)
		efi_error("efi_set_variable() failed");
	return rc;
}

static void
show_mirror(void)
{
	int status;
	int below4g = 0, above4g = 0;
	int rbelow4g = 0, rabove4g = 0;

	if (get_mirror(0, &below4g, &above4g, &status) == 0) {
		if (status == 0) {
			printf("MirroredPercentageAbove4G: %d.%.2d\n",
			       above4g/100, above4g%100);
			printf("MirrorMemoryBelow4GB: %s\n",
			       below4g ? "true" : "false");
		} else {
			printf("MirrorStatus: ");
			switch (status) {
			case 1:
				printf("Platform does not support address range mirror\n");
				break;
			case 2:
				printf("Invalid version number\n");
				break;
			case 3:
				printf("MirroredMemoryAbove4GB > 50.00%%\n");
				break;
			case 4:
				printf("DIMM configuration does not allow mirror\n");
				break;
			case 5:
				printf("OEM specific method\n");
				break;
			default:
				printf("%u\n", status);
				break;
			}
			printf("DesiredMirroredPercentageAbove4G: %d.%.2d\n",
			       above4g/100, above4g%100);
			printf("DesiredMirrorMemoryBelow4GB: %s\n",
			       below4g ? "true" : "false");
		}
	}
	if ((get_mirror(1, &rbelow4g, &rabove4g, &status) == 0) &&
	    (above4g != rabove4g || below4g != rbelow4g)) {
		printf("RequestMirroredPercentageAbove4G: %d.%.2d\n",
		       rabove4g/100, rabove4g%100);
		printf("RequestMirrorMemoryBelow4GB: %s\n",
		       rbelow4g ? "true" : "false");
	}
}

static void
usage()
{
	printf("efibootmgr version %s\n", EFIBOOTMGR_VERSION);
	printf("usage: efibootmgr [options]\n");
	printf("\t-a | --active         Set bootnum active.\n");
	printf("\t-A | --inactive       Set bootnum inactive.\n");
	printf("\t-b | --bootnum XXXX   Modify BootXXXX (hex).\n");
	printf("\t-B | --delete-bootnum Delete bootnum.\n");
	printf("\t-c | --create         Create new variable bootnum and add to bootorder at index (-I).\n");
	printf("\t-C | --create-only    Create new variable bootnum and do not add to bootorder.\n");
	printf("\t-d | --disk disk      Disk containing boot loader (defaults to /dev/sda).\n");
	printf("\t-D | --remove-dups    Remove duplicate values from BootOrder.\n");
	printf("\t-e | --edd [1|3]      Force boot entries to be created using EDD 1.0 or 3.0 info.\n");
	printf("\t-E | --device num     EDD 1.0 device number (defaults to 0x80).\n");
	printf("\t     --full-dev-path  Use a full device path.\n");
	printf("\t     --file-dev-path  Use an abbreviated File() device path.\n");
	printf("\t-f | --reconnect      Re-connect devices after driver is loaded.\n");
	printf("\t-F | --no-reconnect   Do not re-connect devices after driver is loaded.\n");
	printf("\t-g | --gpt            Force disk with invalid PMBR to be treated as GPT.\n");
	printf("\t-i | --iface name     Create a netboot entry for the named interface.\n");
	printf("\t-I | --index number   When creating an entry, insert it in bootorder at specified position (default: 0).\n");
	printf("\t-l | --loader name     (Defaults to \""DEFAULT_LOADER"\").\n");
	printf("\t-L | --label label     Boot manager display label (defaults to \"Linux\").\n");
	printf("\t-m | --mirror-below-4G t|f Mirror memory below 4GB.\n");
	printf("\t-M | --mirror-above-4G X Percentage memory to mirror above 4GB.\n");
	printf("\t-n | --bootnext XXXX   Set BootNext to XXXX (hex).\n");
	printf("\t-N | --delete-bootnext Delete BootNext.\n");
	printf("\t-o | --bootorder XXXX,YYYY,ZZZZ,...     Explicitly set BootOrder (hex).\n");
	printf("\t-O | --delete-bootorder Delete BootOrder.\n");
	printf("\t-p | --part part        Partition containing loader (defaults to 1 on partitioned devices).\n");
	printf("\t-q | --quiet            Be quiet.\n");
	printf("\t-r | --driver           Operate on Driver variables, not Boot Variables.\n");
	printf("\t-t | --timeout seconds  Set boot manager timeout waiting for user input.\n");
	printf("\t-T | --delete-timeout   Delete Timeout.\n");
	printf("\t-u | --unicode | --UCS-2  Handle extra args as UCS-2 (default is ASCII).\n");
	printf("\t-v | --verbose          Print additional information.\n");
	printf("\t-V | --version          Return version and exit.\n");
	printf("\t-w | --write-signature  Write unique sig to MBR if needed.\n");
	printf("\t-y | --sysprep          Operate on SysPrep variables, not Boot Variables.\n");
	printf("\t-@ | --append-binary-args file  Append extra args from file (use \"-\" for stdin).\n");
	printf("\t-h | --help             Show help/usage.\n");
}

static void
set_default_opts()
{
	memset(&opts, 0, sizeof(opts));
	opts.num             = -1;   /* auto-detect */
	opts.bootnext        = -1;   /* Don't set it */
	opts.active          = -1;   /* Don't set it */
	opts.reconnect       = -1;   /* Don't set it */
	opts.timeout         = -1;   /* Don't set it */
	opts.edd10_devicenum = 0x80;
	opts.loader          = DEFAULT_LOADER;
	opts.label           = (unsigned char *)"Linux";
	opts.disk            = "/dev/sda";
	opts.part            = -1;
}

static void
parse_opts(int argc, char **argv)
{
	int c, rc;
	unsigned int num;
	int snum;
	float fnum;
	int option_index = 0;
	long lindex;

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
			{"create-only",            no_argument, 0, 'C'},
			{"disk",             required_argument, 0, 'd'},
			{"remove-dups",            no_argument, 0, 'D'},
			{"edd",              required_argument, 0, 'e'},
			{"edd30",            required_argument, 0, 'e'},
			{"edd-device",       required_argument, 0, 'E'},
			{"full-dev-path",          no_argument, 0, 0},
			{"file-dev-path",          no_argument, 0, 0},
			{"reconnect",              no_argument, 0, 'f'},
			{"no-reconnect",           no_argument, 0, 'F'},
			{"gpt",                    no_argument, 0, 'g'},
			{"iface",            required_argument, 0, 'i'},
			{"index",            required_argument, 0, 'I'},
			{"keep",                   no_argument, 0, 'k'},
			{"loader",           required_argument, 0, 'l'},
			{"label",            required_argument, 0, 'L'},
			{"mirror-below-4G",  required_argument, 0, 'm'},
			{"mirror-above-4G",  required_argument, 0, 'M'},
			{"bootnext",         required_argument, 0, 'n'},
			{"delete-bootnext",        no_argument, 0, 'N'},
			{"bootorder",        required_argument, 0, 'o'},
			{"delete-bootorder",       no_argument, 0, 'O'},
			{"part",             required_argument, 0, 'p'},
			{"quiet",                  no_argument, 0, 'q'},
			{"driver",                 no_argument, 0, 'r'},
			{"timeout",          required_argument, 0, 't'},
			{"delete-timeout",         no_argument, 0, 'T'},
			{"unicode",                no_argument, 0, 'u'},
			{"UCS-2",                  no_argument, 0, 'u'},
			{"verbose",          optional_argument, 0, 'v'},
			{"version",                no_argument, 0, 'V'},
			{"write-signature",        no_argument, 0, 'w'},
			{"sysprep",                no_argument, 0, 'y'},
			{"append-binary-args", required_argument, 0, '@'},
			{"help",                   no_argument, 0, 'h'},
			{0, 0, 0, 0}
		};

		c = getopt_long(argc, argv,
				"aAb:BcCd:De:E:fFgi:kl:L:m:M:n:No:Op:qrt:Tuv::Vwy@:h",
				long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
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
			opts.delete = 1;
			break;
		case 'b': {
			char *endptr = NULL;
			unsigned long result;

			if (!optarg) {
				errorx(29, "--%s requires an argument",
				       long_options[option_index]);
				break;
			}

			result = strtoul(optarg, &endptr, 16);
			if ((result == ULONG_MAX && errno == ERANGE) ||
					(endptr && *endptr != '\0')) {
				off_t offset = (intptr_t)endptr
					       - (intptr_t)optarg;
				print_error_arrow(optarg, offset,
						  "Invalid bootnum value");
				conditional_error_reporter(opts.verbose >= 1,
							   1);
				exit(28);
			}
			if (result > 0xffff)
				errorx(29, "Invalid bootnum value: %lX\n",
				       result);

			opts.num = result;
			break;
		}
		case 'c':
			opts.create = 1;
			break;
		case 'C':
			opts.create = 1;
			opts.no_order = 1;
			break;
		case 'D':
			opts.deduplicate = 1;
			break;
		case 'd':
			opts.disk = optarg;
			break;
		case 'e':
			rc = sscanf(optarg, "%d", &snum);
			if (rc != 1)
				errorx(30, "invalid numeric value %s\n",
				       optarg);

			if (snum != EFIBOOTMGR_PATH_ABBREV_EDD10 &&
			    snum != EFIBOOTMGR_PATH_ABBREV_NONE)
				errorx(31, "invalid EDD version %d\n", snum);

			if (opts.abbreviate_path != EFIBOOTMGR_PATH_ABBREV_UNSPECIFIED &&
			    opts.abbreviate_path != snum)
				errx(41, "contradicting --full-device-path/--file-device-path/-e options");

			opts.abbreviate_path = snum;
			break;
		case 'E':
			rc = sscanf(optarg, "%x", &num);
			if (rc == 1)
				opts.edd10_devicenum = num;
			else
				errorx(32, "invalid hex value %s\n", optarg);
			break;
		case 'f':
			opts.reconnect = 1;
			break;
		case 'F':
			opts.reconnect = 0;
			break;
		case 'g':
			opts.forcegpt = 1;
			break;

		case 'h':
			usage();
			exit(0);
			break;

		case 'i':
			opts.iface = optarg;
			opts.ip_version = EFIBOOTMGR_IPV4;
			opts.ip_addr_origin = EFIBOOTMGR_IPV4_ORIGIN_DHCP;
			break;
		case 'I':
			if (!optarg) {
				errorx(1, "--%s requires an argument",
				       long_options[option_index]);
			}
			lindex = atol(optarg);
			if (lindex < 0 || lindex > UINT16_MAX) {
				errorx(1, "invalid numeric value %s\n",
				       optarg);
			}
			opts.index = (uint16_t)lindex;
			break;
		case 'k':
			opts.keep_old_entries = 1;
			break;
		case 'l':
			opts.loader = optarg;
			break;
		case 'L':
			opts.label = (unsigned char *)optarg;
			opts.explicit_label = 1;
			break;
		case 'm':

			if (!optarg) {
				errorx(33, "--%s requires an argument",
				       long_options[option_index]);
				break;
			}

			opts.set_mirror_lo = 1;

			switch (optarg[0]) {
			case '1': case 'y': case 't':
				opts.below4g = 1;
				break;
			case '0': case 'n': case 'f':
				opts.below4g = 0;
				break;
			default:
				errorx(33, "invalid boolean value %s\n",
				       optarg);
			}
			break;
		case 'M':
			opts.set_mirror_hi = 1;
			rc = sscanf(optarg, "%f", &fnum);
			if (rc == 1 && fnum <= 50 && fnum >= 0)
				/* percent to basis points */
				opts.above4g = fnum * 100;
			else
				errorx(34, "invalid numeric value %s\n",
				       optarg);
			break;
		case 'N':
			opts.delete_bootnext = 1;
			break;
		case 'n': {
			char *endptr = NULL;
			unsigned long result;

			if (!optarg) {
				errorx(36, "--%s requires an argument",
				       long_options[option_index]);
				break;
			}

			result = strtoul(optarg, &endptr, 16);
			if ((result == ULONG_MAX && errno == ERANGE) ||
					(endptr && *endptr != '\0')) {
				off_t offset = (intptr_t)endptr
					       - (intptr_t)optarg;
				print_error_arrow(optarg, offset,
						  "Invalid BootNext value");
				conditional_error_reporter(opts.verbose >= 1,
							   1);
				exit(35);
			}
			if (result > 0xffff)
				errorx(36, "Invalid BootNext value: %lX\n",
				       result);
			opts.bootnext = result;
			break;
		}
		case 'o':
			opts.order = optarg;
			break;
		case 'O':
			opts.delete_order = 1;
			break;
		case 'p':
			rc = sscanf(optarg, "%u", &num);
			if (rc == 1)
				opts.part = num;
			else
				errorx(37, "invalid numeric value %s\n",
				       optarg);
			break;
		case 'q':
			opts.quiet = 1;
			break;
		case 'r':
			opts.driver = 1;
			break;
		case 't':
			rc = sscanf(optarg, "%u", &num);
			if (rc == 1) {
				opts.timeout = num;
				opts.set_timeout = 1;
			} else {
				errorx(38, "invalid numeric value %s\n",
				       optarg);
			}
			break;
		case 'T':
			opts.delete_timeout = 1;
			break;
		case 'u':
			opts.unicode = 1;
			break;
		case 'v':
			opts.verbose += 1;
			if (optarg) {
				if (!strcmp(optarg, "v"))
					opts.verbose = 1;
				if (!strcmp(optarg, "vv"))
					opts.verbose = 2;
				rc = sscanf(optarg, "%u", &num);
				if (rc == 1)
					opts.verbose = num;
				else
					errorx(39,
					       "invalid numeric value %s\n",
					       optarg);
			}
			efi_set_verbose(opts.verbose - 1, stderr);
			break;
		case 'V':
			opts.showversion = 1;
			break;

		case 'w':
			opts.write_signature = 1;
			break;

		case 'y':
			opts.sysprep = 1;
			break;

		default:
			if (!strcmp(long_options[option_index].name, "full-dev-path")) {
				if (opts.abbreviate_path != EFIBOOTMGR_PATH_ABBREV_UNSPECIFIED &&
				    opts.abbreviate_path != EFIBOOTMGR_PATH_ABBREV_NONE)
					errx(41, "contradicting --full-dev-path/--file-dev-path/-e options");
				opts.abbreviate_path = EFIBOOTMGR_PATH_ABBREV_NONE;
			} else if (!strcmp(long_options[option_index].name, "file-dev-path")) {
				if (opts.abbreviate_path != EFIBOOTMGR_PATH_ABBREV_UNSPECIFIED &&
				    opts.abbreviate_path != EFIBOOTMGR_PATH_ABBREV_FILE)
					errx(41, "contradicting --full-dev-path/--file-dev-path/-e options");
				opts.abbreviate_path = EFIBOOTMGR_PATH_ABBREV_FILE;
			} else {
				usage();
				exit(1);
			}
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
	char **names = NULL;
	var_entry_t *new_entry = NULL;
	int num;
	int ret = 0;
	ebm_mode mode = boot;
	char *prefices[] = {
		"Boot",
		"Driver",
		"SysPrep",
	};
	char *order_name[] = {
		"BootOrder",
		"DriverOrder",
		"SysPrepOrder"
	};

	set_default_opts();
	parse_opts(argc, argv);
	if (opts.showversion) {
		printf("version %s\n", EFIBOOTMGR_VERSION);
		return 0;
	}

	verbose = opts.verbose;

	if (opts.sysprep && opts.driver)
		errx(25, "--sysprep and --driver may not be used together.");

	if (opts.sysprep || opts.driver) {
		if (opts.bootnext >= 0 || opts.delete_bootnext)
			errx(26, "%s mode does not support BootNext options.",
			     opts.sysprep ? "--sysprep": "--driver");

		if (opts.timeout >= 0 || opts.delete_timeout)
			errx(27, "%s mode does not support timeout options.",
			     opts.sysprep ? "--sysprep": "--driver");

		if (opts.sysprep)
			mode = sysprep;

		if (opts.driver)
			mode = driver;
	}

	if (opts.reconnect > 0 && !opts.driver)
		errorx(30, "--reconnect is supported only for driver entries.");

	if (!efi_variables_supported())
		errorx(2, "EFI variables are not supported on this system.");


	read_var_names(prefices[mode], &names);
	read_vars(names, &entry_list);
	set_var_nums(prefices[mode], &entry_list);

	if (opts.delete) {
		if (opts.num == -1 && opts.explicit_label == 0) {
			errorx(3,
			       "You must specify an entry to delete (see the -b option or -L option).");
		} else {
			if (opts.num != -1) {
				ret = delete_var(prefices[mode], opts.num);
				if (ret < 0)
					error(15, "Could not delete variable");
			} else {
				ret = delete_label(prefices[mode], opts.label);
				if (ret < 0)
					errorx(15, "Could not delete variable");
			}
		}
	}

	if (opts.active >= 0) {
		if (opts.num == -1) {
			errorx(4,
			       "You must specify a entry to activate (see the -b option)");
		} else {
			ret = set_active_state(prefices[mode]);
			if (ret < 0)
				error(16,
				  "Could not set active state for %s%04X",
				  prefices[mode], opts.num);
		}
	}

	if (opts.reconnect >= 0) {
		if (opts.num == -1) {
			errorx(4,
			       "You must specify a driver entry to set re-connect on (see the -b option)");
		} else {
			ret = set_force_reconnect(prefices[mode]);
			if (ret < 0)
				error(16,
				      "Could not set re-connect for %s%04X",
				      prefices[mode], opts.num);
		}
	}

	if (opts.create) {
		warn_duplicate_name(&entry_list);
		new_entry = make_var(prefices[mode], &entry_list);
		if (!new_entry)
			error(5, "Could not prepare %s variable",
			      prefices[mode]);

		/* Put this boot var in the right Order variable */
		if (new_entry && !opts.no_order) {
			ret = add_to_order(order_name[mode], new_entry->num,
					   opts.index);
			if (ret < 0)
				error(6, "Could not add entry to %s",
				      order_name[mode]);
		}
	} else if (opts.index) {
		error(1, "Index is meaningless without create");
	}

	if (opts.delete_order) {
		ret = efi_del_variable(EFI_GLOBAL_GUID, order_name[mode]);
		if (ret < 0 && errno != ENOENT)
			error(7, "Could not remove entry from %s",
			      order_name[mode]);
	}

	if (opts.order) {
		ret = set_order(order_name[mode], prefices[mode],
				opts.keep_old_entries);
		if (ret < 0)
			error(8, "Could not set %s", order_name[mode]);
	}

	if (opts.deduplicate) {
		ret = remove_dupes_from_order(order_name[mode]);
		if (ret)
			error(9, "Could not remove duplicates from %s order",
			      order_name[mode]);
	}

	if (opts.delete_bootnext) {
		if (!is_current_entry(opts.delete_bootnext))
			errorx(17, "Boot entry %04X does not exist",
				opts.delete_bootnext);

		ret = efi_del_variable(EFI_GLOBAL_GUID, "BootNext");
		if (ret < 0)
			error(10, "Could not delete BootNext");
	}

	if (opts.delete_timeout) {
		ret = efi_del_variable(EFI_GLOBAL_GUID, "Timeout");
		if (ret < 0)
			error(11, "Could not delete Timeout");
	}

	if (opts.bootnext >= 0) {
		if (!is_current_entry(opts.bootnext & 0xFFFF))
			errorx(12, "Boot entry %X does not exist",
			       opts.bootnext);
		ret = set_u16("BootNext", opts.bootnext & 0xFFFF);
		if (ret < 0)
			error(13, "Could not set BootNext");
	}

	if (opts.set_timeout) {
		ret = set_u16("Timeout", opts.timeout);
		if (ret < 0)
			error(14, "Could not set Timeout");
	}

	if (opts.set_mirror_lo || opts.set_mirror_hi) {
		ret=set_mirror(opts.below4g, opts.above4g);
	}

	if (!opts.quiet && ret == 0) {
		switch (mode) {
		case boot:
			num = read_u16("BootNext");
			cond_warning(opts.verbose >= 2 && num < 0,
				     "Could not read variable 'BootNext'");
			if (num >= 0)
				printf("BootNext: %04X\n", num);
			num = read_u16("BootCurrent");
			cond_warning(opts.verbose >= 2 && num < 0,
				     "Could not read variable 'BootCurrent'");
			if (num >= 0)
				printf("BootCurrent: %04X\n", num);
			num = read_u16("Timeout");
			cond_warning(opts.verbose >= 2 && num < 0,
				     "Could not read variable 'Timeout'");
			if (num >= 0)
				printf("Timeout: %u seconds\n", num);
			show_order(order_name[mode]);
			show_vars(prefices[mode]);
			show_mirror();
			break;
		case driver:
		case sysprep:
			show_order(order_name[mode]);
			show_vars(prefices[mode]);
			break;
		}
	}
	free_vars(&entry_list);
	free_array(names);
	if (ret)
		return 1;
	return 0;
}
