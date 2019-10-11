/*
  Copyright 2001 Dell Computer Corporation <Matt_Domsch@dell.com>
  Copyright 2014-2019 Peter Jones <pjones@redhat.com>

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

#ifndef _PARSE_LOADER_DATA_H
#define _PARSE_LOADER_DATA_H

#include <stdint.h>
#include "efi.h"

ssize_t parse_efi_guid(char *buffer, size_t buffer_size,
		       uint8_t *p, uint64_t length);
ssize_t parse_raw_text(char *buffer, size_t buffer_size,
		       uint8_t *p, uint64_t length);

#endif
