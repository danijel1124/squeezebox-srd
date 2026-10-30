/*
 * Force-included (-include) when building espeak-ng with the SqueezeOS
 * toolchain (Sourcery G++ Lite 2010q1, gcc 4.4), which predates
 * __has_include. espeak-ng only asks for headers glibc 2.11 has, except
 * <sys/endian.h> (only tried after <endian.h>) and <valgrind/memcheck.h>
 * (skipped by defining MAKE_MEM_UNDEFINED here).
 */
#ifndef __has_include
#define __has_include(x) 1
#endif
#ifndef __has_include_next
#define __has_include_next(x) 1
#endif
#ifndef MAKE_MEM_UNDEFINED
#define MAKE_MEM_UNDEFINED(addr, len) ((void) ((void) addr, len))
#endif
