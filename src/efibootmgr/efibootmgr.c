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


  This must tie the EFI_DEVICE_PATH to /boot/efi/elilo.efi
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <getopt.h>
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
	struct dirent *name;
	uint16_t       num;
	efi_variable_t var_data;
	list_t         list;
} var_entry_t;


/* global variables */
static	LIST_HEAD(boot_entry_list);
static	LIST_HEAD(blk_list);
efibootmgr_opt_t opts;

static inline void
var_num_from_name(const char *pattern, char *name, uint16_t *num)
{
	sscanf(name, pattern, num);
}

static void
fill_bootvar_name(char *dest, size_t len, const char *name)
{
	efi_guid_t guid = EFI_GLOBAL_VARIABLE;
	char text_uuid[40];
	efi_guid_unparse(&guid, text_uuid);
	snprintf(dest, len, "%s-%s", name, text_uuid);
}

static void
fill_var(efi_variable_t *var, const char *name)
{
	efi_guid_t guid = EFI_GLOBAL_VARIABLE;

	efichar_from_char(var->VariableName, name, 1024);
	memcpy(&var->VendorGuid, &guid, sizeof(guid));
	var->Attributes = EFI_VARIABLE_NON_VOLATILE
		| EFI_VARIABLE_BOOTSERVICE_ACCESS
		| EFI_VARIABLE_RUNTIME_ACCESS;
}


static void
read_vars(struct dirent **namelist,
	  int num_boot_names,
	  list_t *head)
{
	efi_status_t status;
	var_entry_t *entry;
	int i;

	if (!namelist) return;

	for (i=0; i < num_boot_names; i++)
	{
		if (namelist[i]) {
			entry = malloc(sizeof(var_entry_t));
			if (!entry) return;
			memset(entry, 0, sizeof(var_entry_t));

			status = read_variable(namelist[i]->d_name,
					       &entry->var_data);
			if (status != EFI_SUCCESS) break;
			entry->name = namelist[i];
			list_add_tail(&entry->list, head);
		}
	}
	return;
}





static void
free_dirents(struct dirent **ptr, int num_dirents)
{
	int i;
	if (!ptr) return;
	for (i=0; i < num_dirents; i++) {
		if (ptr[i]) {
			free(ptr[i]);
			ptr[i] = NULL;
		}
	}
	free(ptr);
}



static int
compare(const void *a, const void *b)
{
	int rc = -1;
	uint32_t n1, n2;
	memcpy(&n1, a, sizeof(n1));
	memcpy(&n2, b, sizeof(n2));
	if (n1 < n2) rc = -1;
	if (n1 == n2) rc = 0;
	if (n2 > n2) rc = 1;
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
	var_entry_t *boot;
	list_for_each(pos, boot_list) {
		num_vars++;
	}
	vars = malloc(sizeof(uint16_t) * num_vars);
	if (!vars) return -1;
	memset(vars, 0, sizeof(uint16_t) * num_vars);

	list_for_each(pos, boot_list) {
		boot = list_entry(pos, var_entry_t, list);
		vars[i] = boot->num;
			i++;
	}
	qsort(vars, i, sizeof(uint16_t), compare);
	found = 1;

	num_vars = i;
	for (free_number = 0; free_number < num_vars && found; free_number++) {
		found = 0;
		list_for_each(pos, boot_list) {
			boot = list_entry(pos, var_entry_t, list);
			if (boot->num == free_number) {
				found = 1;
				break;
			}
		}
		if (!found) break;
	}
	if (found && num_vars) free_number = vars[num_vars-1] + 1;
	free(vars);
	return free_number;
}


static void
warn_duplicate_name(list_t *boot_list)
{
	list_t *pos;
	var_entry_t *boot;
	EFI_LOAD_OPTION *load_option;

	list_for_each(pos, boot_list) {
		boot = list_entry(pos, var_entry_t, list);
		load_option = (EFI_LOAD_OPTION *)
			boot->var_data.Data;
		if (!efichar_char_strcmp(opts.label,
					 load_option->description)) {
			fprintf(stderr, "** Warning ** : %.8s has same label %s\n",
			       boot->name->d_name,
			       opts.label);
		}
	}
}


static var_entry_t *
make_boot_var(list_t *boot_list)
{
	var_entry_t *boot;
	int free_number;
	list_t *pos;

	if (opts.bootnum == -1)
		free_number = find_free_boot_var(boot_list);
	else {
		list_for_each(pos, boot_list) {
			boot = list_entry(pos, var_entry_t, list);
			if (boot->num == opts.bootnum) {
				fprintf(stderr, "** Warning ** : bootnum %04X "
				        "already exists\n", opts.bootnum);
				return NULL;
			}
		}
		free_number = opts.bootnum;
	}

	if (free_number == -1) return NULL;

	/* Create a new var_entry_t object
	   and populate it.
	*/

	boot = malloc(sizeof(*boot));
	if (!boot) return NULL;
	memset(boot, 0, sizeof(*boot));
	boot->num = free_number;
	if (!make_linux_efi_variable(&boot->var_data, free_number)) {
		free(boot);
		return NULL;
	}
	create_variable(&boot->var_data);
	list_add_tail(&boot->list, boot_list);
	return boot;
}



static efi_status_t
read_boot(efi_variable_t *var, const char *name)
{
	char name_guid[PATH_MAX];

	memset(var, 0, sizeof(*var));
	fill_bootvar_name(name_guid, sizeof(name_guid), name);
	return read_variable(name_guid, var);
}

static efi_status_t
read_boot_order(efi_variable_t *boot_order)
{
	efi_status_t status;

	status = read_boot(boot_order, "BootOrder");
	if (status != EFI_SUCCESS && status != EFI_NOT_FOUND)
		return status;

	if (status == EFI_NOT_FOUND) {
		fill_var(boot_order, "BootOrder");
	}
	return EFI_SUCCESS;
}


static efi_status_t
add_to_boot_order(uint16_t num)
{
	efi_status_t status;
	efi_variable_t boot_order;
	uint64_t new_data_size;
	uint16_t *new_data, *old_data;

	status = read_boot_order(&boot_order);
	if (status != EFI_SUCCESS) return status;

	/* We've now got an array (in boot_order.Data) of the
	   boot order.  First add our entry, then copy the old array.
	*/
	old_data = (uint16_t *)&(boot_order.Data);
	new_data_size = boot_order.DataSize + sizeof(uint16_t);
	new_data = malloc(new_data_size);

	new_data[0] = num;
	memcpy(new_data+1, old_data, boot_order.DataSize);

	/* Now new_data has what we need */
	memcpy(&(boot_order.Data), new_data, new_data_size);
	boot_order.DataSize = new_data_size;
	return create_or_edit_variable(&boot_order);
}


static efi_status_t
remove_from_boot_order(uint16_t num)
{
	efi_status_t status;
	efi_variable_t boot_order;
	uint64_t new_data_size;
	uint16_t *new_data, *old_data;
	int old_i,new_i;
	char boot_order_name[PATH_MAX];

	status = read_boot_order(&boot_order);
	if (status != EFI_SUCCESS) return status;
	/* If it's empty, yea! */
	if (!boot_order.DataSize) return EFI_SUCCESS;

	fill_bootvar_name(boot_order_name, sizeof(boot_order_name),
			  "BootOrder");

	/* We've now got an array (in boot_order.Data) of the
	   boot order.  Simply copy the array, skipping the
	   entry we're deleting.
	*/
	old_data = (uint16_t *)&(boot_order.Data);
	/* Start with the same size */
	new_data_size = boot_order.DataSize;
	new_data = malloc(new_data_size);
	for (old_i=0,new_i=0;
	     old_i < boot_order.DataSize / sizeof(uint16_t);
	     old_i++) {
		if (old_data[old_i] != num) {
				/* Copy this value */
			new_data[new_i] = old_data[old_i];
			new_i++;
		}
	}

	/* Now new_data has what we need */
	new_data_size = new_i * sizeof(uint16_t);
	memset(&(boot_order.Data), 0, boot_order.DataSize);
	memcpy(&(boot_order.Data), new_data, new_data_size);
	boot_order.DataSize = new_data_size;

	return edit_variable(&boot_order);
}

static efi_status_t
delete_var(const char *name)
{
	efi_variable_t var;

	memset(&var, 0, sizeof(var));
	fill_var(&var, name);
	return delete_variable(&var);
}

static int
read_boot_u16(const char *name)
{
	efi_status_t status;
	efi_variable_t var;
	uint16_t *n = (uint16_t *)(var.Data);

	memset(&var, 0, sizeof(var));
	status = read_boot(&var, name);
	if (status) return -1;
	return *n;
}

static efi_status_t
set_boot_u16(const char *name, uint16_t num)
{
	efi_variable_t var;
	uint16_t *n = (uint16_t *)var.Data;

	memset(&var, 0, sizeof(var));

	fill_var(&var, name);
	*n = num;
	var.DataSize = sizeof(uint16_t);
	return create_or_edit_variable(&var);
}

static efi_status_t
delete_boot_var(uint16_t num)
{
	efi_status_t status;
	efi_variable_t var;
	char name[16];
	list_t *pos, *n;
	var_entry_t *boot;

	snprintf(name, sizeof(name), "Boot%04X", num);
	memset(&var, 0, sizeof(var));
	fill_var(&var, name);
	status = delete_variable(&var);

	/* For backwards compatibility, try to delete abcdef entries as well */
	if (status) {
		snprintf(name, sizeof(name), "Boot%04x", num);
		memset(&var, 0, sizeof(var));
		fill_var(&var, name);
		status = delete_variable(&var);
	}

	if (status) return status;
	list_for_each_safe(pos, n, &boot_entry_list) {
		boot = list_entry(pos, var_entry_t, list);
		if (boot->num == num) {
			status = remove_from_boot_order(num);
			if (status) return status;
			list_del(&(boot->list));
			break; /* short-circuit since it was found */
		}
	}
	return EFI_SUCCESS;
}


static void
set_var_nums(const char *pattern, list_t *list)
{
	list_t *pos;
	var_entry_t *var;
	int num=0, rc;
	char *name;
	int warn=0;

	list_for_each(pos, list) {
		var = list_entry(pos, var_entry_t, list);
		rc = sscanf(var->name->d_name, pattern, &num);
		if (rc == 1) {
			var->num = num;
			name = var->name->d_name; /* shorter name */
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

#if 0
static efi_variable_t *
find_pci_scsi_disk_blk(int fd, int bus, int device, int func,
		       list_t *blk_list)
{
	list_t *pos;
	int rc;
	Scsi_Idlun idlun;
	unsigned char host, channel, id, lun;
	var_entry_t *blk;
	efi_variable_t *blk_var;
	long size = 0;

	memset(&idlun, 0, sizeof(idlun));
	rc = get_scsi_idlun(fd, &idlun);
	if (rc) return NULL;

	rc = disk_get_size(fd, &size);

	idlun_to_components(&idlun, &host, &channel, &id, &lun);

	list_for_each(pos, blk_list) {
		blk = list_entry(pos, var_entry_t, list);
		blk_var = blk->var_data;

		if (!compare_pci_scsi_disk_blk(blk_var,
					       bus, device, func,
					       host, channel, id, lun,
					       0, size)) {
			return blk_var;
		}
	}
	return NULL;
}




/* The right blkX variable contains:
   1) the PCI and SCSI information for the disk passed in disk_name
   2) Does not contain a partition field 
*/


static efi_variable_t *
find_disk_blk(char *disk_name, list_t *blk_list)
{
	efi_variable_t *disk_blk = NULL;
	int fd, rc;
	unsigned char bus=0,device=0,func=0;
	int interface_type=interface_type_unknown;
	unsigned int controllernum=0, disknum=0;
	unsigned char part=0;

	fd = open(disk_name, O_RDONLY|O_DIRECT);
	rc = disk_get_pci(fd, &bus, &device, &func);
	if (rc) {
		fprintf(stderr, "disk_get_pci() failed.\n");
		return NULL;
	}
	rc = disk_info_from_fd(fd,
			       &interface_type,
			       &controllernum,
			       &disknum,
			       &part);
	if (rc) {
		fprintf(stderr, "disk_info_from_fd() failed.\n");
		return NULL;
	}
	switch (interface_type)
	{
	case scsi:
		return find_pci_scsi_disk_blk(fd,bus,device,func,blk_list);
		break;
	case ata:
		return find_pci_ata_disk_blk(fd,bus,device,func,blk_list);
		break;
	case i2o:
		return find_pci_i2o_disk_blk(fd,bus,device,func,blk_list);
		break;
	case md:
		return find_pci_md_disk_blk(fd,bus,device,func,blk_list);
		break;
	default:
		break;
	}
	return NULL;
}
#endif

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
parse_boot_order(char *buffer, uint16_t *order, int length)
{
	int i;
	int num, rc;

	for (i=0; i<length && *buffer; i++) {
		rc = sscanf(buffer, "%x", &num);
		if (rc == 1) order[i] = num & 0xFFFF;
		/* Advance to the comma */ 
		while (*buffer && *buffer != ',') buffer++;
		/* Advance through the comma(s) */
		while (*buffer && *buffer == ',') buffer++;
	}
	return i;
}

static efi_status_t
set_boot_order()
{
	efi_variable_t boot_order;
	uint16_t *n = (uint16_t *)boot_order.Data;

	if (!opts.bootorder) return EFI_SUCCESS;

	memset(&boot_order, 0, sizeof(boot_order));
	fill_var(&boot_order, "BootOrder");

	boot_order.DataSize = parse_boot_order(opts.bootorder, n, 1024/sizeof(uint16_t)) * sizeof(uint16_t);
	return create_or_edit_variable(&boot_order);
}

static void
show_boot_vars()
{
	list_t *pos;
	var_entry_t *boot;
	char description[80];
	EFI_LOAD_OPTION *load_option;
	EFI_DEVICE_PATH *path;
	char text_path[1024], *p;
	unsigned long optional_data_len=0;

	list_for_each(pos, &boot_entry_list) {
		boot = list_entry(pos, var_entry_t, list);
		load_option = (EFI_LOAD_OPTION *)
			boot->var_data.Data;
		efichar_to_char(description,
				load_option->description, sizeof(description));
		memset(text_path, 0, sizeof(text_path));
		path = load_option_path(load_option);
		if (boot->name)
			printf("%.8s", boot->name->d_name);
		else
			printf("Boot%04X", boot->num);

		if (load_option->attributes & LOAD_OPTION_ACTIVE)
			printf("* ");
		else    printf("  ");
		printf("%s", description);

		if (opts.verbose) {
			unparse_path(text_path, path,
				     load_option->file_path_list_length);
			/* Print optional data */
			optional_data_len =
				boot->var_data.DataSize -
				load_option->file_path_list_length -
				((char *)path - (char *)load_option);
			if (optional_data_len) {
				p = text_path;
				p += strlen(text_path);
				unparse_raw_text(p, ((uint8_t *)path) +
						 load_option->file_path_list_length,
						 optional_data_len);
			}

			printf("\t%s", text_path);
		}
		printf("\n");
	}
}



static void
show_boot_order()
{
	efi_status_t status;
	efi_variable_t boot_order;
	uint16_t *data;

	status = read_boot_order(&boot_order);

	if (status != EFI_SUCCESS) {
		perror("show_boot_order()");
		return;
	}

	/* We've now got an array (in boot_order.Data) of the
	   boot order.  First add our entry, then copy the old array.
	*/
	data = (uint16_t *)&(boot_order.Data);
	if (boot_order.DataSize)
		unparse_boot_order(data, boot_order.DataSize / sizeof(uint16_t));

}

static efi_status_t
set_active_state()
{
	list_t *pos;
	var_entry_t *boot;
	EFI_LOAD_OPTION *load_option;

	list_for_each(pos, &boot_entry_list) {
		boot = list_entry(pos, var_entry_t, list);
		load_option = (EFI_LOAD_OPTION *)
			boot->var_data.Data;
		if (boot->num == opts.bootnum) {
			if (opts.active == 1) {
				if (load_option->attributes
				    & LOAD_OPTION_ACTIVE) return EFI_SUCCESS;
				else {
					load_option->attributes
						|= LOAD_OPTION_ACTIVE;
					return edit_variable(&boot->var_data);
				}
			}
			else if (opts.active == 0) {
				if (!(load_option->attributes
				      & LOAD_OPTION_ACTIVE))
					return EFI_SUCCESS;
				else {
					load_option->attributes
						&= ~LOAD_OPTION_ACTIVE;
					return edit_variable(&boot->var_data);
				}
			}
		}
	}
	return EFI_SUCCESS;
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
	printf("\t-d | --disk disk       (defaults to /dev/sda) containing loader\n");
	printf("\t-e | --edd [1|3|-1]   force EDD 1.0 or 3.0 creation variables, or guess\n");
	printf("\t-E | --device num      EDD 1.0 device number (defaults to 0x80)\n");
	printf("\t-g | --gpt            force disk with invalid PMBR to be treated as GPT\n");
	printf("\t-H | --acpi_hid XXXX  set the ACPI HID (used with -i)\n");
	printf("\t-i | --iface name     create a netboot entry for the named interface\n");
	printf("\t-l | --loader name     (defaults to \\elilo.efi)\n");
	printf("\t-L | --label label     Boot manager display label (defaults to \"Linux\")\n");
	printf("\t-n | --bootnext XXXX   set BootNext to XXXX (hex)\n");
	printf("\t-N | --delete-bootnext delete BootNext\n");
	printf("\t-o | --bootorder XXXX,YYYY,ZZZZ,...     explicitly set BootOrder (hex)\n");
	printf("\t-O | --delete-bootorder delete BootOrder\n");
	printf("\t-p | --part part        (defaults to 1) containing loader\n");
	printf("\t-q | --quiet            be quiet\n");
	printf("\t   | --test filename    don't write to NVRAM, write to filename.\n");
	printf("\t-t | --timeout seconds  set boot manager timeout waiting for user input.\n");
	printf("\t-T | --delete-timeout   delete Timeout.\n");
	printf("\t-u | --unicode | --UCS-2  pass extra args as UCS-2 (default is ASCII)\n");
	printf("\t-U | --acpi_uid XXXX    set the ACPI UID (used with -i)\n");
	printf("\t-v | --verbose          print additional information\n");
	printf("\t-V | --version          return version and exit\n");
	printf("\t-w | --write-signature  write unique sig to MBR if needed\n");
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
	opts.loader          = "\\elilo.efi";
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
	int c, num, rc;
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
			{"disk",             required_argument, 0, 'd'},
			{"iface",            required_argument, 0, 'i'},
			{"acpi_hid",         required_argument, 0, 'H'},
			{"edd-device",       required_argument, 0, 'E'},
			{"edd30",            required_argument, 0, 'e'},
			{"gpt",                    no_argument, 0, 'g'},
			{"loader",           required_argument, 0, 'l'},
			{"label",            required_argument, 0, 'L'},
			{"bootnext",         required_argument, 0, 'n'},
			{"delete-bootnext",        no_argument, 0, 'N'},
			{"bootorder",        required_argument, 0, 'o'},
			{"delete-bootorder",       no_argument, 0, 'O'},
			{"part",             required_argument, 0, 'p'},
			{"quiet",                  no_argument, 0, 'q'},
			{"test",             required_argument, 0,   1},
			{"timeout",          required_argument, 0, 't'},
			{"delete-timeout",         no_argument, 0, 'T'},
			{"unicode",                no_argument, 0, 'u'},
			{"UCS-2",                  no_argument, 0, 'u'},
			{"acpi_uid",         required_argument, 0, 'U'},
			{"verbose",          optional_argument, 0, 'v'},
			{"version",                no_argument, 0, 'V'},
			{"write-signature",        no_argument, 0, 'w'},
			{0, 0, 0, 0}
		};

		c = getopt_long (argc, argv,
				 "AaBb:cd:e:E:gH:i:l:L:n:No:Op:qt:TuU:v::Vw",
				 long_options, &option_index);
		if (c == -1)
			break;

		switch (c)
		{
		case 'a':
			opts.active = 1;
			break;
		case 'A':
			opts.active = 0;
			break;
		case 'B':
			opts.delete_boot = 1;
			break;
		case 'b':
			rc = sscanf(optarg, "%X", &num);
			if (rc == 1) opts.bootnum = num;
			break;
		case 'c':
			opts.create = 1;
			break;
		case 'd':
			opts.disk = optarg;
			break;
		case 'e':
			rc = sscanf(optarg, "%d", &num);
			if (rc == 1) opts.edd_version = num;
			break;
		case 'E':
			rc = sscanf(optarg, "%x", &num);
			if (rc == 1) opts.edd10_devicenum = num;
			break;
		case 'g':
			opts.forcegpt = 1;
			break;
		case 'H':
			rc = sscanf(optarg, "%x", &num);
			if (rc == 1) opts.acpi_hid = num;
			break;
		case 'i':
			opts.iface = optarg;
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
		case 'n':
			rc = sscanf(optarg, "%x", &num);
			if (rc == 1) opts.bootnext = num;
			break;
		case 'o':
			opts.bootorder = optarg;
			break;
		case 'O':
			opts.delete_bootorder = 1;
			break;
		case 'p':
			rc = sscanf(optarg, "%u", &num);
			if (rc == 1) opts.part = num;
			break;
		case 'q':
			opts.quiet = 1;
			break;
		case 1:
			opts.testfile = optarg;
			break;
		case 't':
			rc = sscanf(optarg, "%u", &num);
			if (rc == 1) {
				opts.timeout = num;
				opts.set_timeout = 1;
			}
			break;
		case 'T':
			opts.delete_timeout = 1;
			break;
		case 'u':
			opts.unicode = 1;
			break;

		case 'U':
			rc = sscanf(optarg, "%x", &num);
			if (rc == 1) opts.acpi_uid = num;
			break;
		case 'v':
			opts.verbose = 1;
			if (optarg) {
				if (!strcmp(optarg, "v"))  opts.verbose = 2;
				if (!strcmp(optarg, "vv")) opts.verbose = 3;
				rc = sscanf(optarg, "%d", &num);
				if (rc == 1)  opts.verbose = num;
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
	struct dirent  **boot_names = NULL;
	var_entry_t *new_boot = NULL;
	int num, num_boot_names=0;

	set_default_opts();
	parse_opts(argc, argv);
	if (opts.showversion) {
		printf("version %s\n", EFIBOOTMGR_VERSION);
		return 0;
	}

	if (opts.iface && opts.acpi_hid == -1 && opts.acpi_uid == -1) {
		fprintf(stderr, "\nYou must specify the ACPI HID and UID when using -i.\n\n");
		return 1;
	}

	if (!opts.testfile)
		set_fs_kernel_calls();

	if (!opts.testfile) {
		num_boot_names = read_boot_var_names(&boot_names);
		read_vars(boot_names, num_boot_names, &boot_entry_list);
		set_var_nums("Boot%04X-%*s", &boot_entry_list);

		if (opts.delete_boot) {
			if (opts.bootnum == -1)
				fprintf(stderr, "\nYou must specify a boot entry to delete (see the -b option).\n\n");
			else
				delete_boot_var(opts.bootnum);
		}

		if (opts.active >= 0) {
			set_active_state();
		}
	}

	if (opts.create) {
		warn_duplicate_name(&boot_entry_list);
		new_boot = make_boot_var(&boot_entry_list);
		/* Put this boot var in the right BootOrder */
		if (!opts.testfile && new_boot)
			add_to_boot_order(new_boot->num);
	}

	if (!opts.testfile) {

		if (opts.delete_bootorder) {
			delete_var("BootOrder");
		}

		if (opts.bootorder) {
			set_boot_order();
		}


		if (opts.delete_bootnext) {
			delete_var("BootNext");
		}

		if (opts.delete_timeout) {
			delete_var("Timeout");
		}

		if (opts.bootnext >= 0) {
			set_boot_u16("BootNext", opts.bootnext & 0xFFFF);
		}

		if (opts.set_timeout) {
			set_boot_u16("Timeout", opts.timeout);
		}

		if (!opts.quiet) {
			num = read_boot_u16("BootNext");
			if (num != -1 ) {
				printf("BootNext: %04X\n", num);
			}
			num = read_boot_u16("BootCurrent");
			if (num != -1) {
				printf("BootCurrent: %04X\n", num);
			}
			num = read_boot_u16("Timeout");
			if (num != -1) {
				printf("Timeout: %u seconds\n", num);
			}
			show_boot_order();
			show_boot_vars();
		}
	}
	free_dirents(boot_names, num_boot_names);
	return 0;
}

