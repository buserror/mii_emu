/*
 * mii_disk_format.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <sys/stat.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "mii_disk_format.h"


#define byte uint8_t

static const size_t nib_disksz = 232960;
static const size_t dsk_disksz = 143360;

extern DiskFormatDesc nib_insert(const char*, byte *, size_t);
extern DiskFormatDesc dsk_insert(const char *, byte *, size_t);
extern DiskFormatDesc empty_disk_desc;

int mmapfile(const char *fname, byte **buf, size_t *sz, int flags);

DiskFormatDesc disk_format_load(const char *path)
{
    if (path == NULL) {
        return empty_disk_desc;
    }
    byte *buf;
    size_t sz;
    int err = mmapfile(path, &buf, &sz, O_RDWR);
    if (buf == NULL) {
        DIE(1,"Couldn't load/mmap disk %s: %s\n",
            path, strerror(err));
    }
	printf("%s loaded %s dz = %d\n", __func__, path, (int)sz);
    if (sz == nib_disksz) {
        return nib_insert(path, buf, sz);
    } else if (sz == dsk_disksz) {
        return dsk_insert(path, buf, sz);
    } else {
        DIE(2,"Unrecognized disk format for %s.\n", path);
    }
}

int mmapfile(const char *fname, byte **buf, size_t *sz, int flags)
{
    int err;
    int fd;

    *buf = NULL;

    errno = 0;
    fd = open(fname, flags);
    if (fd < 0) {
        return errno;
    }

    struct stat st;
    errno = 0;
    err = fstat(fd, &st);
    if (err < 0) {
        goto bail;
    }

    errno = 0;
    int protect = PROT_READ;
    int mflags = MAP_PRIVATE;
    if (flags & O_RDWR || flags & O_WRONLY) {
        protect |= PROT_WRITE;
        mflags = MAP_SHARED;
    }
    *buf = mmap(NULL, st.st_size, protect, mflags, fd, 0);
    if (*buf == NULL) {
        err = errno;
        goto bail;
    }
    close(fd); // safe to close now.

    *sz = st.st_size;
    return 0;
bail:
    close(fd);
    return err;
}
