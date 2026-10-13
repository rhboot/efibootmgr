// SPDX-License-Identifier: GPL-2.0-or-later

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "shim-hive.h"

static uint32_t
get_le32(const uint8_t *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static void
put_le32(uint8_t *p, uint32_t value)
{
	p[0] = value;
	p[1] = value >> 8;
	p[2] = value >> 16;
	p[3] = value >> 24;
}

static uint32_t
shim_hive_crc32(const uint8_t *data, size_t len)
{
	uint32_t crc = UINT32_MAX;
	size_t i;
	unsigned int bit;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^
			      (0xedb88320U & (0U - (crc & 1U)));
	}
	return crc ^ UINT32_MAX;
}

static int
checked_add(size_t *value, size_t add)
{
	if (*value > SIZE_MAX - add) {
		errno = EOVERFLOW;
		return -1;
	}
	*value += add;
	return 0;
}

ssize_t
shim_hive_serialize(const struct shim_hive_item *items, size_t n_items,
		    uint8_t *data, size_t data_size)
{
	size_t needed = SHIM_HIVE_HEADER_SIZE;
	size_t off;
	size_t i;
	struct shim_hive_header *hdr = (struct shim_hive_header *)data;

	if ((!items && n_items) || n_items > UINT8_MAX) {
		errno = EINVAL;
		return -1;
	}

	for (i = 0; i < n_items; i++) {
		size_t key_len;

		if (!items[i].key || (!items[i].value && items[i].value_len)) {
			errno = EINVAL;
			return -1;
		}
		key_len = strlen(items[i].key);
		if (key_len == 0 || key_len > UINT8_MAX ||
		    items[i].value_len > UINT32_MAX) {
			errno = E2BIG;
			return -1;
		}
		if (checked_add(&needed, SHIM_HIVE_ITEM_HEADER_SIZE) < 0 ||
		    checked_add(&needed, key_len) < 0 ||
		    checked_add(&needed, items[i].value_len) < 0)
			return -1;
	}

	if (needed > SSIZE_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	if (!data)
		return needed;
	if (data_size < needed) {
		errno = ENOSPC;
		return -1;
	}

	memset(data, 0, needed);
	memcpy(hdr->magic, "HIVE", 4);
	hdr->version = SHIM_HIVE_HEADER_VERSION;
	hdr->items_count = n_items;
	hdr->items_offset = SHIM_HIVE_HEADER_SIZE;

	off = SHIM_HIVE_HEADER_SIZE;
	for (i = 0; i < n_items; i++) {
		size_t key_len = strlen(items[i].key);

		data[off++] = key_len;
		put_le32(data + off, items[i].value_len);
		off += sizeof(uint32_t);
		memcpy(data + off, items[i].key, key_len);
		off += key_len;
		if (items[i].value_len) {
			memcpy(data + off, items[i].value, items[i].value_len);
			off += items[i].value_len;
		}
	}

	/*
	 * The buffer was zeroed above, so the CRC field is zero while the
	 * checksum is calculated.
	 */
	put_le32((uint8_t *)&hdr->crc32, shim_hive_crc32(data, off));
	return off;
}

static int
shim_hive_measure(const uint8_t *data, size_t data_size, size_t *hive_size)
{
	size_t off;
	unsigned int i;
	struct shim_hive_header *hdr = (struct shim_hive_header *)data;
	struct shim_hive_item_header *item = NULL;

	if (!hdr || data_size < SHIM_HIVE_HEADER_SIZE ||
	    memcmp(hdr->magic, "HIVE", 4) != 0) {
		errno = EINVAL;
		return -1;
	}

	if (hdr->version < SHIM_HIVE_HEADER_VERSION ||
	    hdr->items_offset < SHIM_HIVE_HEADER_SIZE || hdr->items_offset > data_size) {
		errno = EINVAL;
		return -1;
	}

	off = hdr->items_offset;
	item = (struct shim_hive_item_header *) &data[off];

	for (i = 0; i < hdr->items_count; i++) {
		if (off > data_size ||
		    data_size - off < SHIM_HIVE_ITEM_HEADER_SIZE) {
			errno = EINVAL;
			return -1;
		}
		if (item->key_len == 0 || (SHIM_HIVE_ITEM_HEADER_SIZE + item->key_len +
			get_le32((uint8_t *)&item->value_len)) > data_size - off) {
			errno = EINVAL;
			return -1;
		}
		off += (SHIM_HIVE_ITEM_HEADER_SIZE + item->key_len + get_le32((uint8_t *)&item->value_len));
		item = (struct shim_hive_item_header *) &data[off];
	}

	*hive_size = off;
	return 0;
}

int
shim_hive_validate(const uint8_t *data, size_t data_size)
{
	uint8_t *copy;
	uint32_t stored_crc;
	uint32_t calculated_crc;
	size_t hive_size;
	struct shim_hive_header *hdr = (struct shim_hive_header *)data;

	if (shim_hive_measure(data, data_size, &hive_size) < 0)
		return -1;

	copy = malloc(hive_size);

	if (!copy)
		return -1;

	memcpy(copy, data, hive_size);
	stored_crc = get_le32((uint8_t *)&hdr->crc32);

	((struct shim_hive_header *)copy)->crc32 = 0;

	calculated_crc = shim_hive_crc32(copy, hive_size);

	free(copy);

	if (stored_crc != calculated_crc) {
		errno = EBADMSG;
		return -1;
	}
	return 0;
}

char *
shim_hive_format(const uint8_t *data, size_t data_size)
{
	size_t hive_size;
	size_t off;
	size_t output_len = sizeof(" Hive()") - 1;
	unsigned int i;
	char *output;
	char *p;
	struct shim_hive_header *hdr = (struct shim_hive_header *)data;
	struct shim_hive_item_header *item_hdr = NULL;
	struct shim_hive_item item;

	if (shim_hive_validate(data, data_size) < 0 ||
	    shim_hive_measure(data, data_size, &hive_size) < 0)
		return NULL;

	off = hdr->items_offset;

	item_hdr = (struct shim_hive_item_header *)&data[off];

	for (i = 0; i < hdr->items_count; i++) {
		if (checked_add(&output_len, item_hdr->key_len) < 0 ||
		    checked_add(&output_len, get_le32((uint8_t*)&item_hdr->value_len)) < 0)
			return NULL;

		off += item_hdr->key_len + get_le32((uint8_t*)&item_hdr->value_len) + sizeof(struct shim_hive_item_header);
		item_hdr = (struct shim_hive_item_header *)&data[off];
	}
	// Add ',' '=' ' '
	output_len += hdr->items_count * 2;

	output = calloc(1, output_len);

	if (!output)
		return NULL;
	p = stpcpy(output, " Hive(");

	item_hdr = (struct shim_hive_item_header *)&data[hdr->items_offset];
	item.key = (const char *)((uint8_t *)item_hdr +
			sizeof(struct shim_hive_item_header));
	item.value = (const uint8_t *)((uint8_t *)item_hdr +
			sizeof(struct shim_hive_item_header) + item_hdr->key_len);

	for (i = 0; i < hdr->items_count; i++) {

		if (i)
			*p++ = ',';

		memcpy(p, item.key, item_hdr->key_len);
		p += item_hdr->key_len;
		*p++ = '=';

		memcpy(p, item.value, get_le32((uint8_t*)&item_hdr->value_len));
		p += get_le32((uint8_t*)&item_hdr->value_len);

		item_hdr = (struct shim_hive_item_header *) ((uint8_t *)item_hdr +
				sizeof(*item_hdr) +
				item_hdr->key_len +
				get_le32((uint8_t*)&item_hdr->value_len));

		item.key = (const char *)((uint8_t *)item_hdr +
				sizeof(struct shim_hive_item_header));
		item.value = (const uint8_t *)((uint8_t *)item_hdr +
				sizeof(struct shim_hive_item_header) + item_hdr->key_len);
	}
	*p++ = ')';
	*p = '\0';
	return output;
}
