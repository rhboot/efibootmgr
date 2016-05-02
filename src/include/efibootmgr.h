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

#define EFIBOOTMGR_IPV4 0
#define EFIBOOTMGR_IPV6 1

#define EFIBOOTMGR_IPV4_ORIGIN_DHCP		0
#define EFIBOOTMGR_IPV4_ORIGIN_STATIC		1
#define EFIBOOTMGR_IPV6_ORIGIN_STATIC		0
#define EFIBOOTMGR_IPV6_ORIGIN_STATELESS	1
#define EFIBOOTMGR_IPV6_ORIGIN_STATEFUL		2

typedef enum {
	boot,
	driver,
	sysprep,
} ebm_mode;

typedef struct {
	int argc;
	char **argv;
	int optind;
	char *disk;

	int ip_version;
	char *iface;
	char *macaddr;
	char *local_ip_addr;
	char *remote_ip_addr;
	char *gateway_ip_addr;
	char *ip_netmask;
	uint16_t ip_local_port;
	uint16_t ip_remote_port;
	uint16_t ip_protocol;
	uint8_t ip_addr_origin;

	char *loader;
	unsigned char *label;
	char *order;
	int keep_old_entries;
	char *testfile;
	char *extra_opts_file;
	uint32_t part;
	int edd_version;
	uint32_t edd10_devicenum;
	int num;
	int bootnext;
	int verbose;
	int active;
	int below4g;
	int above4g;
	int deduplicate;
	unsigned int delete:1;
	unsigned int delete_order:1;
	unsigned int delete_bootnext:1;
	unsigned int quiet:1;
	unsigned int showversion:1;
	unsigned int create:1;
	unsigned int unicode:1;
	unsigned int write_signature:1;
	unsigned int forcegpt:1;
	unsigned int set_timeout:1;
	unsigned int delete_timeout:1;
	unsigned int set_mirror_lo:1;
	unsigned int set_mirror_hi:1;
	unsigned int no_order:1;
	unsigned int driver:1;
	unsigned int sysprep:1;
	short int timeout;
} efibootmgr_opt_t;

extern efibootmgr_opt_t opts;

#endif
