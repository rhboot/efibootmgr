/*
  efivars_sysfs.[ch] - Manipulates EFI variables as exported in /sys/firmware/efi/vars

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

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <fcntl.h>

#include "efi.h"
#include "efichar.h"
#include "efibootmgr.h"
#include "efivars_sysfs.h"

static efi_status_t
sysfs_read_variable(const char *name, efi_variable_t *var)
{
	int newnamesize;
	char *newname;
	int fd;
	size_t readsize;
	if (!name || !var) return EFI_INVALID_PARAMETER;

	newnamesize = strlen(SYSFS_DIR_EFI_VARS) + strlen(name) + 2;
	newname = malloc(newnamesize);
	if (!newname) return EFI_OUT_OF_RESOURCES;
	sprintf(newname, "%s/%s", SYSFS_DIR_EFI_VARS,name);
	fd = open(newname, O_RDONLY);
	if (fd == -1) {
		free(newname);
		return EFI_NOT_FOUND;
	}
	readsize = read(fd, var, sizeof(*var));
	if (readsize != sizeof(*var)) {
		free(newname);
		close(fd);
		return EFI_INVALID_PARAMETER;
	}
	close(fd);
	free(newname);
	return var->Status;
}

static efi_status_t
sysfs_write_variable(const char *name, efi_variable_t *var)
{
	int fd;
	size_t writesize;
	char buffer[PATH_MAX+40];

	if (!name || !var) return EFI_INVALID_PARAMETER;
	memset(buffer, 0, sizeof(buffer));

	fd = open(name, O_WRONLY);
	if (fd == -1) {
		sprintf(buffer, "sysfs_write_variable():open(%s)", name);
		perror(buffer);
		return EFI_INVALID_PARAMETER;
	}
	writesize = write(fd, var, sizeof(*var));
	if (writesize != sizeof(*var)) {
		close(fd);
		return EFI_INVALID_PARAMETER;

	}
	close(fd);
	return EFI_SUCCESS;
}


static efi_status_t
sysfs_edit_variable(const char *name, efi_variable_t *var)
{
	int newnamesize;
	char *newname;
	efi_status_t status;
	if (!name || !var) return EFI_INVALID_PARAMETER;

	newnamesize = strlen(SYSFS_DIR_EFI_VARS) + strlen(name) + 2;
	newname = malloc(newnamesize);
	if (!newname) return EFI_OUT_OF_RESOURCES;
	sprintf(newname, "%s/%s", SYSFS_DIR_EFI_VARS,name);

	status = sysfs_write_variable(newname, var);
	free(newname);
	return status;
}

static efi_status_t
sysfs_create_variable(efi_variable_t *var)
{
	int newnamesize;
	char *newname;
	efi_status_t status;
	if (!var) return EFI_INVALID_PARAMETER;

	newnamesize = strlen(SYSFS_DIR_EFI_VARS) + strlen("new_var") + 2;
	newname = malloc(newnamesize);
	if (!newname) return EFI_OUT_OF_RESOURCES;
	sprintf(newname, "%s/%s", SYSFS_DIR_EFI_VARS,"new_var");

	status = sysfs_write_variable(newname, var);
	free(newname);
	return status;
}

static efi_status_t
sysfs_delete_variable(efi_variable_t *var)
{
	int newnamesize;
	char *newname;
	efi_status_t status;
	if (!var) return EFI_INVALID_PARAMETER;

	newnamesize = strlen(SYSFS_DIR_EFI_VARS) + strlen("del_var") + 2;
	newname = malloc(newnamesize);
	if (!newname) return EFI_OUT_OF_RESOURCES;
	sprintf(newname, "%s/%s", SYSFS_DIR_EFI_VARS,"del_var");

	status = sysfs_write_variable(newname, var);
	free(newname);
	return status;
}



struct efivar_kernel_calls sysfs_kernel_calls = {
	.read = sysfs_read_variable,
	.edit = sysfs_edit_variable,
	.create = sysfs_create_variable,
	.delete = sysfs_delete_variable,
	.path = SYSFS_DIR_EFI_VARS,
};
