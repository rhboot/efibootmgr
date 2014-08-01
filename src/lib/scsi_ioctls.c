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
#include <sys/pci.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/nvme.h>
#include "scsi_ioctls.h"

int
get_nvme_ns_id(int fd, uint32_t *ns_id)
{
	return ioctl(fd, NVME_IOCTL_ID, &ns_id);
}

int
idlun_to_components (Scsi_Idlun *idlun,
		     unsigned char *host,
		     unsigned char *channel,
		     unsigned char *id,
		     unsigned char *lun)
{
	if (!idlun || !host || !channel || !id || !lun) return 1;

	*host    = (idlun->dev_id >> 24) & 0xFF;
	*channel = (idlun->dev_id >> 16) & 0xFF;
	*id      = (idlun->dev_id      ) & 0xFF;
	*lun     = (idlun->dev_id >>  8) & 0xFF;
	return 0;
}


int
get_scsi_idlun(int fd, Scsi_Idlun *idlun)
{
	return ioctl(fd, SCSI_IOCTL_GET_IDLUN, idlun);
}

int
get_scsi_pci(int fd, char *slot_name, size_t size)
{
	char buf[SLOT_NAME_SIZE] = "";
	int rc;

	rc = ioctl(fd, SCSI_IOCTL_GET_PCI, buf);
	if (rc == 0) {
		snprintf(slot_name, size, "%s", buf);
	}
	return rc;
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
	char slot_name[SLOT_NAME_SIZE] = "unknown";
	int fd = 0, rc = 0;

	memset(&idlun, 0, sizeof(idlun));

	if (argc < 2) {usage(argv); exit(1);}

	fd = open(argv[1], O_RDONLY);
	if (fd == -1) {
		perror("Unable to open file");
		exit(1);
	}

	rc = get_scsi_pci(fd, slot_name, sizeof slot_name);
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
