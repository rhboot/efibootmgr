/*
  scsi_ioctls.[ch]
 
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

#ifndef _SCSI_IOCTLS_H
#define _SCSI_IOCTLS_H 

#include <stdint.h>


/* Snagged from linux/include/scsi/scsi.h */
#define SCSI_IOCTL_GET_IDLUN 0x5382
#define SCSI_IOCTL_GET_PCI   0x5387

typedef struct scsi_idlun {
	uint32_t dev_id;
	uint32_t host_unique_id;
} Scsi_Idlun;


inline int get_scsi_idlun(int fd, Scsi_Idlun *idlun);
inline int get_scsi_pci(int fd, char *slot_name);
int idlun_to_components (Scsi_Idlun *idlun,
			 unsigned char *host,
			 unsigned char *channel,
			 unsigned char *id,
			 unsigned char *lun);

#endif
