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

#include <stdio.h>
#include <pci/pci.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include "scsi_ioctls.h"

int
idlun_to_components (Scsi_Idlun *idlun,
		     unsigned char *host,
		     unsigned char *channel,
		     unsigned char *id,
		     unsigned char *lun)
{
	*host    = (idlun->dev_id >> 24) & 0xFF;
	*channel = (idlun->dev_id >> 16) & 0xFF;
	*id      = (idlun->dev_id      ) & 0xFF;
	*lun     = (idlun->dev_id >>  8) & 0xFF;
}


inline int
get_scsi_idlun(int fd, Scsi_Idlun *idlun)
{
	return ioctl(fd, SCSI_IOCTL_GET_IDLUN, idlun);
}

inline int 
get_scsi_pci(int fd, char *slot_name)
{
	return ioctl(fd, SCSI_IOCTL_GET_PCI, slot_name);
}



#ifdef SCSI_IOCTLS_EXE
static void
usage(char **argv)
{
	printf("Usage: %s /dev/sdX    where sdX is a SCSI device node.\n",
	       argv[0]);
}

int main(int argc, char **argv)
{
	Scsi_Idlun idlun;
	char slot_name[8];
	int fd = 0, rc = 0;

	memset(&idlun, 0, sizeof(idlun));

	if (argc < 2) {usage(argv); exit(1);}
	
	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("Unable to open file");
		exit(1);
	}

	rc = get_scsi_pci(fd, slot_name);
	if (rc) {
		perror("Unable to get_scsi_pci()");
	}
	rc = get_scsi_idlun(fd, &idlun);
	if (rc) {
		perror("Unable to get_scsi_idlun()");
	}
	
	printf("Device: %s\n", argv[1]);
	printf("PCI: %s\n", slot_name);

	printf("SCSI: host %d channel %d id %d lun %d, unique ID %x\n",
	       (idlun.dev_id >> 24) & 0xFF, // host
	       (idlun.dev_id >> 16) & 0xFF, // channel
	       idlun.dev_id  & 0xFF,        // id
	       (idlun.dev_id >>  8) & 0xFF, // lun
	       idlun.host_unique_id);

	return 0;
}
#endif
