/*                  M A P P E D F I L E . C P P
 * BRL-CAD
 *
 * Copyright (c) 2023 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following
 * disclaimer in the documentation and/or other materials provided
 * with the distribution.
 *
 * 3. The name of the author may not be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/** @file MappedFile.cpp
 *
 * Simple cross platform wrapper for mapping files
 *
 */

#include "MappedFile.hpp"

#if defined __GNUC__
#  define GCC_PREREQ(major, minor) __GNUC__ > (major) || (__GNUC__ == (major) && __GNUC_MINOR__ >= (minor))
#else
#  define GCC_PREREQ(major, minor) 0
#endif

#if defined __INTEL_COMPILER
#  define ICC_PREREQ(version) (__INTEL_COMPILER >= (version))
#else
#  define ICC_PREREQ(version) 0
#endif

#if GCC_PREREQ(3, 0) || ICC_PREREQ(800)
#  define UNLIKELY(expression) __builtin_expect((expression), 0)
#else
#  define UNLIKELY(expression) (expression)
#endif

#if defined(_WIN32) && !defined(__CYGWIN__)
#  ifdef WIN32_LEAN_AND_MEAN
#    undef WIN32_LEAN_AND_MEAN
#  endif
#  define WIN32_LEAN_AND_MEAN 434144 /* don't want winsock.h */

#  ifdef NOMINMAX
#    undef NOMINMAX
#  endif
#  define NOMINMAX 434144 /* don't break std::min and std::max */

#  include <windows.h>

#  undef WIN32_LEAN_AND_MEAN /* unset to not interfere with calling apps */
#  undef NOMINMAX
#  include <io.h>
#  include <fcntl.h>

#  define O_RDONLY _O_RDONLY
#  define O_BINARY _O_BINARY

#else

#  ifdef HAVE_SYS_STAT_H
#    include <sys/stat.h>
#  endif

#  ifdef HAVE_SYS_MMAN_H
#    include <sys/mman.h>
#  endif

#  ifdef HAVE_FCNTL_H
#    include <fcntl.h>
#  endif

#  ifdef HAVE_UNISTD_H
#    include <unistd.h>
#  endif

#  define O_BINARY 0
#endif

#if !defined(MAP_FAILED)
#  define MAP_FAILED ((void *)-1)     /* Error return from mmap() */
#endif

/* off_t is 32 bit size even on 64 bit Windows. In the past we have tried to
 * force off_t to be 64 bit but this is failing on newer Windows/Visual Studio
 * versions in 2020 - therefore, we instead introduce the b_off_t define to
 * properly substitute the correct numerical type for the correct platform.  */
#if defined(_WIN64)
#  include <sys/stat.h>
#  define b_off_t __int64
#  define fstat _fstati64
#  define stat  _stati64
#elif defined (_WIN32)
#  include <sys/stat.h>
#  define b_off_t _off_t
#  define fstat _fstat
#  define stat  _stat
#else
#  define b_off_t off_t
#endif

/* Based on the public domain wrapper implemented by Mike Frysinger:
 * https://cgit.uclibc-ng.org/cgi/cgit/uclibc-ng.git/tree/utils/mmap-windows.c */
#ifdef _WIN32
#  define PROT_READ     0x1
#  define PROT_WRITE    0x2
/* This flag is only available in WinXP+ */
#  ifdef FILE_MAP_EXECUTE
#    define PROT_EXEC     0x4
#  else
#    define PROT_EXEC        0x0
#    define FILE_MAP_EXECUTE 0
#  endif

#  define MAP_SHARED    0x01
#  define MAP_PRIVATE   0x02
#  define MAP_ANONYMOUS 0x20
#  define MAP_ANON      MAP_ANONYMOUS
#  define MAP_FAILED    ((void *) -1)

#  ifdef HAVE_OFF_T_64BIT
#    define DWORD_HI(x) (x >> 32)
#    define DWORD_LO(x) ((x) & 0xffffffff)
#  else
#    define DWORD_HI(x) (0)
#    define DWORD_LO(x) (x)
#  endif


static void *
win_mmap(void *start, size_t length, int prot, int flags, int fd, b_off_t offset, void **handle)
{
    if (prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC))
	return MAP_FAILED;
    if (fd == -1) {
	if (!(flags & MAP_ANON) || offset)
	    return MAP_FAILED;
    }
    else if (flags & MAP_ANON)
	return MAP_FAILED;

    DWORD flProtect;
    if (prot & PROT_WRITE) {
	if (prot & PROT_EXEC)
	    flProtect = PAGE_EXECUTE_READWRITE;
	else
	    flProtect = PAGE_READWRITE;
    }
    else if (prot & PROT_EXEC) {
	if (prot & PROT_READ)
	    flProtect = PAGE_EXECUTE_READ;
	else if (prot & PROT_EXEC)
	    flProtect = PAGE_EXECUTE;
    }
    else
	flProtect = PAGE_READONLY;

    b_off_t end = length + offset;
    HANDLE mmap_fd, h;
    if (fd == -1)
	mmap_fd = INVALID_HANDLE_VALUE;
    else
	mmap_fd = (HANDLE)_get_osfhandle(fd);
    h = CreateFileMapping(mmap_fd, NULL, flProtect, DWORD_HI(end), DWORD_LO(end), NULL);
    if (h == NULL) {
	return MAP_FAILED;
    } else {
	*handle = (void *)h;
    }

    DWORD dwDesiredAccess;
    if (prot & PROT_WRITE)
	dwDesiredAccess = FILE_MAP_WRITE;
    else
	dwDesiredAccess = FILE_MAP_READ;
    if (prot & PROT_EXEC)
	dwDesiredAccess |= FILE_MAP_EXECUTE;
    if (flags & MAP_PRIVATE)
	dwDesiredAccess |= FILE_MAP_COPY;
    void *ret = MapViewOfFile(h, dwDesiredAccess, DWORD_HI(offset), DWORD_LO(offset), length);
    if (ret == NULL) {
	CloseHandle(h);
	ret = MAP_FAILED;
    }
    return ret;
}


static int
win_munmap(void *addr, size_t length, void *hv)
{
    HANDLE h = (HANDLE)hv;
    UnmapViewOfFile(addr);
    CloseHandle(h);
    return 0;
}
#endif

MappedFile::MappedFile(const char *fname)
{
    buf = NULL;
    buflen = 0;
    handle = NULL;

    if (!fname)
	return;

    name = std::string(name);

    int fd = open(fname, O_RDONLY | O_BINARY);

    if (UNLIKELY(fd < 0))
	return;

    struct stat sb;
    int ret = fstat(fd, &sb);

    if (UNLIKELY(ret < 0) || UNLIKELY(sb.st_size == 0)) {
	(void)close(fd);
	return;
    }

    buflen = sb.st_size;

    /* Attempt to memory-map the file */
#if defined(HAVE_SYS_MMAN_H)
    buf = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
#elif defined(HAVE_WINDOWS_H)
    /* FIXME: shouldn't need to preserve handle */
    buf = win_mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0, &(mp->handle));
#endif /* HAVE_SYS_MMAN_H */

    /* If cannot memory-map, read it in manually */
    if (!buf || buf == MAP_FAILED) {
    	(void)close(fd);
	buf = NULL;
	buflen = 0;
    }
}

MappedFile::~MappedFile()
{
    if (buf) {

	int ret = 0;
#ifdef HAVE_SYS_MMAN_H
	ret = munmap(buf, buflen);
#elif defined(_WIN32)
	ret = win_munmap(buf, buflen, handle);
#endif
    }

    buf = NULL;		/* sanity */
    buflen = 0;		/* sanity */
}


// Local Variables:
// tab-width: 8
// mode: C++
// c-basic-offset: 4
// indent-tabs-mode: t
// c-file-style: "stroustrup"
// End:
// ex: shiftwidth=4 tabstop=8
