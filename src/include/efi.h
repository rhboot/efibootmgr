/*
  efi.[ch] - Extensible Firmware Interface definitions

  Copyright (C) 2001, 2003 Dell Computer Corporation <Matt_Domsch@dell.com>

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

#ifndef EFI_H
#define EFI_H

/*
 * Extensible Firmware Interface
 * Based on 'Extensible Firmware Interface Specification'
 *      version 1.02, 12 December, 2000
 */
#include <stdint.h>
#include <dirent.h>

#include <efivar.h>

/*******************************************************
 * Boot Option Attributes
 *******************************************************/
#define LOAD_OPTION_ACTIVE 0x00000001

/*******************************************************
 * GUIDs
 *******************************************************/
#define BLKX_UNKNOWN_GUID \
EFI_GUID( 0x47c7b225, 0xc42a, 0x11d2, 0x8e57, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

/* Exported functions */

extern int read_boot_var_names(char ***namelist);
extern ssize_t make_linux_load_option(uint8_t **data, size_t *data_size,
		       uint8_t *optional_data, size_t optional_data_size);
extern ssize_t get_extra_args(uint8_t *data, ssize_t data_size);

#endif /* EFI_H */
