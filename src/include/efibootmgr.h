/*
  efibootmgr.h - Manipulates EFI variables as exported in /proc/efi/vars
 
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

#ifndef _EFIBOOTMGR_H
#define _EFIBOOTMGR_H

typedef struct {
	int argc;
	char **argv;
	int optind;
	char *disk;
	char *iface;
	char *loader;
	char *label;
	char *bootorder;
	int keep_old_entries;
	char *testfile;
	char *extra_opts_file;
	uint32_t part;
	int edd_version;
	int edd10_devicenum;
	int bootnum;
	int bootnext;
	int verbose;
	int active;
	int deduplicate;
	int64_t acpi_hid;
	int64_t acpi_uid;
	unsigned int delete_boot:1;
	unsigned int delete_bootorder:1;
	unsigned int delete_bootnext:1;
	unsigned int quiet:1;
	unsigned int showversion:1;
	unsigned int create:1;
	unsigned int unicode:1;
	unsigned int write_signature:1;
	unsigned int forcegpt:1;
	unsigned int set_timeout:1;
	unsigned int delete_timeout:1;
	unsigned short int timeout;
} efibootmgr_opt_t;


extern efibootmgr_opt_t opts;

#endif
