/*
  efivars_procfs.h - EFI Variables accessed through /proc/efi/vars

  Copyright (C) 2003 Dell Computer Corporation <Matt_Domsch@dell.com>

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

#ifndef EFIVARS_PROCFS_H
#define EFIVARS_PROCFS_H

#include "efi.h"

#define PROCFS_DIR_EFI_VARS "/proc/efi/vars"

extern struct efivar_kernel_calls procfs_kernel_calls;

#endif /* EFIVARS_PROCFS_H */
