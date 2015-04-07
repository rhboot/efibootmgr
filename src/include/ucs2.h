#ifndef _EFIVAR_UCS2_H
#define _EFIVAR_UCS2_H

#define ev_bits(val, mask, shift) \
	(((val) & ((mask) << (shift))) >> (shift))

static inline size_t
__attribute__((__unused__))
ucs2len(const uint16_t const *s, ssize_t limit)
{
	ssize_t i;
	for (i = 0; i < (limit >= 0 ? limit : i+1) && s[i] != L'\0'; i++)
		;
	return i;
}

static inline size_t
__attribute__((__unused__))
ucs2size(const uint16_t const *s, ssize_t limit)
{
	size_t rc = ucs2len(s, limit);
	rc *= sizeof (uint16_t);
	rc += sizeof (uint16_t);
	if (limit > 0 && rc > (size_t)limit)
		return limit;
	return rc;
}

static inline size_t
__attribute__((__unused__))
__attribute__((__nonnull__ (1)))
utf8len(uint8_t *s, ssize_t limit)
{
	ssize_t i, j;
	for (i = 0, j = 0; i < (limit >= 0 ? limit : i+1) && s[i] != '\0';
	     j++, i++) {
		if (!(s[i] & 0x80)) {
			;
		} else if ((s[i] & 0xc0) == 0xc0 && !(s[i] & 0x20)) {
			i += 1;
		} else if ((s[i] & 0xe0) == 0xe0 && !(s[i] & 0x10)) {
			i += 2;
		}
	}
	return j;
}

static inline size_t
__attribute__((__unused__))
__attribute__((__nonnull__ (1)))
utf8size(uint8_t *s, ssize_t limit)
{
	size_t ret = utf8len(s,limit);
	if (ret < (limit >= 0 ? (size_t)limit : ret+1))
		ret++;
	return ret;
}

static inline unsigned char *
__attribute__((__unused__))
ucs2_to_utf8(const uint16_t const *chars, ssize_t limit)
{
	ssize_t i, j;
	unsigned char *ret;

	if (limit < 0)
		limit = ucs2len(chars, -1);
	ret = alloca(limit * 6 + 1);
	if (!ret)
		return NULL;
	memset(ret, 0, limit * 6 +1);

	for (i=0, j=0; chars[i] && i < (limit >= 0 ? limit : i+1); i++,j++) {
		if (chars[i] <= 0x7f) {
			ret[j] = chars[i];
		} else if (chars[i] > 0x7f && chars[i] <= 0x7ff) {
			ret[j++] = 0xc0 | ev_bits(chars[i], 0x1f, 6);
			ret[j]   = 0x80 | ev_bits(chars[i], 0x3f, 0);
		} else if (chars[i] > 0x7ff && chars[i] < 0x10000) {
			ret[j++] = 0xe0 | ev_bits(chars[i], 0xf, 12);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 6);
			ret[j]   = 0x80| ev_bits(chars[i], 0x3f, 0);
		} else if (chars[i] > 0xffff && chars[i] < 0x200000) {
			ret[j++] = 0xf0 | ev_bits(chars[i], 0x7, 18);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 12);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 6);
			ret[j]   = 0x80| ev_bits(chars[i], 0x3f, 0);
		} else if (chars[i] > 0x1fffff && chars[i] < 0x4000000) {
			ret[j++] = 0xf8 | ev_bits(chars[i], 0x3, 24);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 18);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 12);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 6);
			ret[j]   = 0x80 | ev_bits(chars[i], 0x3f, 0);
		} else if (chars[i] > 0x3ffffff) {
			ret[j++] = 0xfc | ev_bits(chars[i], 0x1, 30);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 24);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 18);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 12);
			ret[j++] = 0x80 | ev_bits(chars[i], 0x3f, 6);
			ret[j]   = 0x80 | ev_bits(chars[i], 0x3f, 0);
		}
	}
	ret[j] = '\0';
	return (unsigned char *)strdup((char *)ret);
}

static inline ssize_t
__attribute__((__unused__))
__attribute__((__nonnull__ (4)))
utf8_to_ucs2(uint16_t *ucs2, ssize_t size, int terminate, uint8_t *utf8)
{
	ssize_t req;
	ssize_t i, j;

	if (!utf8 || (!ucs2 && size > 0)) {
		errno = EINVAL;
		return -1;
	}

	req = utf8len(utf8, -1) * sizeof (uint16_t);
	if (terminate && req > 0)
		req += 1;

	if (size == 0 || req <= 0)
		return req;

	if (size < req) {
		errno = ENOSPC;
		return -1;
	}

	for (i=0, j=0; i < (size >= 0 ? size : i+1) && utf8[i] != '\0'; j++) {
		uint32_t val = 0;

		if ((utf8[i] & 0xe0) == 0xe0 && !(utf8[i] & 0x10)) {
			val = ((utf8[i+0] & 0x0f) << 10)
			     |((utf8[i+1] & 0x3f) << 6)
			     |((utf8[i+2] & 0x3f) << 0);
			i += 3;
		} else if ((utf8[i] & 0xc0) == 0xc0 && !(utf8[i] & 0x20)) {
			val = ((utf8[i+0] & 0x1f) << 6)
			     |((utf8[i+1] & 0x3f) << 0);
			i += 2;
		} else {
			val = utf8[i] & 0x7f;
			i += 1;
		}
		ucs2[j] = val;
	}
	if (terminate)
		ucs2[j++] = L'\0';
	return j;
};

#endif /* _EFIVAR_UCS2_H */
