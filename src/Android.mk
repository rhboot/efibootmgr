#
# Copyright (C) 2019 The Android-x86 Open Source Project
#
# Licensed under the GNU General Public License Version 2 or later.
# You may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.gnu.org/licenses/gpl.html
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

include $(LOCAL_PATH)/../Make.version

LOCAL_MODULE := efibootmgr
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES := libefivar
LOCAL_MODULE_PATH := $(TARGET_INSTALLER_OUT)/sbin

LOCAL_CFLAGS := \
	-Werror -Wall -Wextra -Wsign-compare -Wstrict-aliasing \
	-D_FILE_OFFSET_BITS=64 -D_GNU_SOURCE \
	-DEFIBOOTMGR_VERSION="\"$(VERSION)\"" \
	-DDEFAULT_LOADER=\"\\\\elilo.efi\"

LOCAL_SRC_FILES := \
	efi.c \
	efibootmgr.c \
	parse_loader_data.c

include $(BUILD_EXECUTABLE)
