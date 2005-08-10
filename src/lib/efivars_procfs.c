/*
  efivars_procfs.[ch] - Manipulates EFI variables as exported in /proc/efi/vars

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
#include "efivars_procfs.h"

static efi_status_t
procfs_read_variable(const char *name, efi_variable_t *var)
{
	char filename[PATH_MAX];
	int fd;
	size_t readsize;
	if (!name || !var) return EFI_INVALID_PARAMETER;

	snprintf(filename, PATH_MAX-1, "%s/%s", PROCFS_DIR_EFI_VARS,name);
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

/**
 * select_variable_names()
 * @d - dirent to compare against
 *
 * This ignores "." and ".." entries, and selects all others.
 */

static int
select_variable_names(const struct dirent *d)
{
	if (!strcmp(d->d_name, ".") ||
	    !strcmp(d->d_name, ".."))
		return 0;
	return 1;
}

/**
 * find_write_victim()
 * @var - variable to be written
 * @file - name of file to open for writing @var is returned.
 *
 * This ignores "." and ".." entries, and selects all others.
 */
static char *
find_write_victim(efi_variable_t *var, char file[PATH_MAX])
{
	struct dirent **namelist = NULL;
	int i, n, found=0;
	char testname[PATH_MAX], *p;

	memset(testname, 0, sizeof(testname));
	n = scandir(PROCFS_DIR_EFI_VARS, &namelist,
		    select_variable_names, alphasort);
	if (n < 0)
		return NULL;

	p = testname;
	efichar_to_char(p, var->VariableName, PATH_MAX);
	p += strlen(p);
	p += sprintf(p, "-");
	efi_guid_unparse(&var->VendorGuid, p);

	for (i=0; i<n; i++) {
		if (namelist[i] &&
		    strncmp(testname, namelist[i]->d_name, sizeof(testname))) {
			found++;
			sprintf(file, "%s/%s", PROCFS_DIR_EFI_VARS,
				namelist[i]->d_name);
			break;
		}
	}

	while (n--) {
		if (namelist[n]) {
			free(namelist[n]);
			namelist[n] = NULL;
		}
	}
	free(namelist);

	if (!found) return NULL;
	return file;
}


static efi_status_t
procfs_write_variable(efi_variable_t *var)
{
	int fd;
	size_t writesize;
	char buffer[PATH_MAX], name[PATH_MAX], *p = NULL;

	if (!var) return EFI_INVALID_PARAMETER;
	memset(buffer, 0, sizeof(buffer));
	memset(name, 0, sizeof(name));

	p = find_write_victim(var, name);
	if (!p) return EFI_INVALID_PARAMETER;

	fd = open(name, O_WRONLY);
	if (fd == -1) {
		sprintf(buffer, "write_variable():open(%s)", name);
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
procfs_delete_variable(efi_variable_t *var)
{
	if (!var) return EFI_INVALID_PARAMETER;
	var->DataSize = 0;
	var->Attributes = 0;
	return procfs_write_variable(var);

}

static efi_status_t
procfs_edit_variable(const char *unused, efi_variable_t *var)
{
	if (!var) return EFI_INVALID_PARAMETER;
	return procfs_write_variable(var);

}

struct efivar_kernel_calls procfs_kernel_calls = {
	.read = procfs_read_variable,
	.edit = procfs_edit_variable,
	.create = procfs_write_variable,
	.delete = procfs_delete_variable,
	.path = PROCFS_DIR_EFI_VARS,
};
