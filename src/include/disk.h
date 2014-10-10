/*
  disk.[ch]
 
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

#ifndef _DISK_H
#define _DISK_H

#include <sys/ioctl.h>
#include <stdint.h>
#include <asm/ioctl.h>
#include <linux/fs.h>

#ifndef BLKGETSIZE
#define BLKGETSIZE _IO(0x12,96)      /* return device size */
#endif
#ifndef BLKSZGET
#define BLKSSZGET  _IO(0x12,104)       /* get block device sector size */
#endif
#ifndef BLKGETSIZE64
#define BLKGETSIZE64 _IOR(0x12,114,uint64_t)   /* return device size in bytes (u64 *arg) */
#endif
#ifndef BLKGETLASTSECT
#define BLKGETLASTSECT _IO(0x12,108) /* get last sector of block device */
#endif


enum _bus_type {bus_type_unknown, isa, pci};
enum _interface_type {interface_type_unknown,
		      ata, atapi, scsi, usb,
		      i1394, fibre, i2o, md,
		      virtblk, nvme};


unsigned int lcm(unsigned int x, unsigned int y);

int disk_get_pci(int fd,
		 int *interface_type,
		 unsigned char *bus,
		 unsigned char *device,
		 unsigned char *function);

struct disk_info {
	int interface_type;
	unsigned int controllernum;
	unsigned int disknum;
	unsigned char part;
	uint64_t major;
	unsigned char minor;
};

int disk_info_from_fd(int fd, struct disk_info *info);


int disk_get_partition_info (int fd, 
			     uint32_t num,
			     uint64_t *start, uint64_t *size,
			     char *signature,
			     uint8_t *mbr_type, uint8_t *signature_type);


int disk_get_size(int fd, long *size);
int get_sector_size(int fd);


#endif
