// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

struct shim_hive_header {
	uint8_t magic[4];	/* HIVE */
	uint8_t version;
	uint8_t items_count;
	uint8_t items_offset;
	uint32_t crc32;
} __attribute__((packed));

struct shim_hive_item_header {
	uint8_t key_len;
	uint32_t value_len;
} __attribute__((packed));

struct shim_hive_item {
	const char *key;
	const uint8_t *value;
	size_t value_len;
};

#define SHIM_HIVE_HEADER_VERSION	1
#define SHIM_HIVE_HEADER_SIZE		sizeof(struct shim_hive_header)
#define SHIM_HIVE_ITEM_HEADER_SIZE	sizeof(struct shim_hive_item_header)

/*
 * Return the number of bytes required to serialize items, or -1 on error.
 * If data is non-NULL, serialize into data after checking data_size.
 */
ssize_t shim_hive_serialize(const struct shim_hive_item *items,
			    size_t n_items, uint8_t *data, size_t data_size);

/*
 * Return true when data begins with a structurally valid ShimHive.
 * The CRC is verified as part of validation.
 */
int shim_hive_validate(const uint8_t *data, size_t data_size);

/*
 * Format a validated hive for efibootmgr's one-line output.
 * The returned string is allocated with malloc(3).
 */
char *shim_hive_format(const uint8_t *data, size_t data_size);

