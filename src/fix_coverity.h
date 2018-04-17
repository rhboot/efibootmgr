/*
 * fix_coverity.h
 * Copyright 2017 Peter Jones <pjones@redhat.com>
 *
 * Distributed under terms of the GPLv3 license.
 */

#ifndef FIX_COVERITY_H
#define FIX_COVERITY_H

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifndef __COVERITY_GCC_VERSION_AT_LEAST
#define __COVERITY_GCC_VERSION_AT_LEAST(x, y) 0
#define FAKE__COVERITY_GCC_VERSION_AT_LEAST__
#endif /* __COVERITY_GCC_VERSION_AT_LEAST */

/* With gcc 7 on x86_64 (at least), coverity pretends to be GCC but
 * accidentally doesn't create all of the types GCC would.
 *
 * In glibc's headers, bits/floatn.h has:
 *
 * #if (defined __x86_64__                                              \
 *   ? __GNUC_PREREQ (4, 3)                                             \
 *   : (defined __GNU__ ? __GNUC_PREREQ (4, 5) : __GNUC_PREREQ (4, 4)))
 * # define __HAVE_FLOAT128 1
 * #else
 * # define __HAVE_FLOAT128 0
 * #endif
 *
 * and stdlib.h has:
 *
 * #if __HAVE_FLOAT128 && __GLIBC_USE (IEC_60559_TYPES_EXT)
 * slash* Likewise for the '_Float128' format  *slash
 * extern _Float128 strtof128 (const char *__restrict __nptr,
 *                       char **__restrict __endptr)
 *      __THROW __nonnull ((1));
 * #endif
 *
 * Which then causes cov-emit to lose its shit:
 *
 * "/usr/include/stdlib.h", line 133: error #20: identifier "_Float128" is
 *           undefined
 *   extern _Float128 strtof128 (const char *__restrict __nptr,
 *          ^
 * "/usr/include/stdlib.h", line 190: error #20: identifier "_Float128" is
 *           undefined
 *                         _Float128 __f)
 *                         ^
 * "/usr/include/stdlib.h", line 236: error #20: identifier "_Float128" is
 *           undefined
 *   extern _Float128 strtof128_l (const char *__restrict __nptr,
 *          ^
 *
 * And then you'll notice something like this later on:
 * [WARNING] Emitted 0 C/C++ compilation units (0%) successfully
 *
 * 0 C/C++ compilation units (0%) are ready for analysis
 *  For more details, please look at:
 *     /home/pjones/devel/github.com/dbxtool/master/cov-int/build-log.txt
 *
 * You would think that if you're writing something that pretends to be
 * gcc, and you've got a "build a configuration by running shit through gcc
 * and looking at the output" stage (which they do), you would run "gcc -da
 * -fdump-tree-all -c -o foo.o foo.c" on an empty file and snarf up all the
 * types defined in the foo.c.001t.tu output.  Apparently, they do not.
 *
 * Anyway, even just defining the type doesn't always work in the face of
 * how _Complex is defined, so we cheat a bit here.  Be prepared to vomit.
 */
#ifdef __x86_64__
#if __COVERITY_GCC_VERSION_AT_LEAST(7, 0)
#if 0
typedef float _Float128 __attribute__((__mode__(__TF__)));
typedef __complex__ float __cfloat128 __attribute__ ((__mode__ (__TC__)));
typedef _Complex float __cfloat128 __attribute__ ((__mode__ (__TC__)));
#else
#include <unistd.h>
#define __cplusplus 201103L
#include <bits/floatn.h>
#undef __cplusplus
#endif
#endif
#endif

#ifdef FAKE__COVERITY_GCC_VERSION_AT_LEAST__
#undef FAKE__COVERITY_GCC_VERSION_AT_LEAST
#undef __COVERITY_GCC_VERSION_AT_LEAST
#endif

#endif /* !FIX_COVERITY_H */
// vim:fenc=utf-8:tw=75
