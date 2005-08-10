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
	char filename[PATH_MAX];
	int fd;
	size_t readsize;
	char buffer[PATH_MAX+40];
	if (!name || !var) return EFI_INVALID_PARAMETER;
	memset(buffer, 0, sizeof(buffer));

	snprintf(filename, PATH_MAX-1, "%s/%s/raw_var", SYSFS_DIR_EFI_VARS,name);
	fd = open(filename, O_RDONLY);
	if (fd == -1) {
		return EFI_NOT_FOUND;
	}
	readsize = read(fd, var, sizeof(*var));
	if (readsize != sizeof(*var)) {
		close(fd);
		return EFI_INVALID_PARAMETER;
	}
	close(fd);
	return var->Status;
}

static efi_status_t
sysfs_write_variable(const char *filename, efi_variable_t *var)
{
	int fd;
	size_t writesize;
	char buffer[PATH_MAX+40];

	if (!filename || !var) return EFI_INVALID_PARAMETER;
	memset(buffer, 0, sizeof(buffer));

	fd = open(filename, O_WRONLY);
	if (fd == -1) {
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
	char filename[PATH_MAX];
	if (!var) return EFI_INVALID_PARAMETER;
	snprintf(filename, PATH_MAX-1, "%s/%s/raw_var", SYSFS_DIR_EFI_VARS,name);
	return sysfs_write_variable(filename, var);
}

static efi_status_t
sysfs_create_variable(efi_variable_t *var)
{
	char filename[PATH_MAX];
	if (!var) return EFI_INVALID_PARAMETER;
	snprintf(filename, PATH_MAX-1, "%s/%s", SYSFS_DIR_EFI_VARS,"new_var");
	return sysfs_write_variable(filename, var);
}

static efi_status_t
sysfs_delete_variable(efi_variable_t *var)
{
	char filename[PATH_MAX];
	if (!var) return EFI_INVALID_PARAMETER;
	snprintf(filename, PATH_MAX-1, "%s/%s", SYSFS_DIR_EFI_VARS,"del_var");
	return sysfs_write_variable(filename, var);
}

struct efivar_kernel_calls sysfs_kernel_calls = {
	.read = sysfs_read_variable,
	.edit = sysfs_edit_variable,
	.create = sysfs_create_variable,
	.delete = sysfs_delete_variable,
	.path = SYSFS_DIR_EFI_VARS,
};
