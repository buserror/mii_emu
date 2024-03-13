/*
 * mii_dd.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "mii_bank.h"
#include "mii_dd.h"
#include "md5.h"

#ifndef FCC
#define FCC(_a,_b,_c,_d) (((_a)<<24)|((_b)<<16)|((_c)<<8)|(_d))
#endif

int
mii_dd_overlay_load(
		mii_dd_t * dd );
int
mii_dd_overlay_prepare(
		mii_dd_t *	dd );


void
mii_dd_system_init(
		struct mii_t *mii,
		mii_dd_system_t *dd )
{
//	printf("*** %s: %p\n", __func__, dd);
	dd->drive = NULL;
	dd->file = NULL;
}

void
mii_dd_system_dispose(
		mii_dd_system_t *dd )
{
//	printf("*** %s: %p\n", __func__, dd);
	while (dd->file)
		mii_dd_file_dispose(dd, dd->file);
	dd->file = NULL;
	dd->drive = NULL;
}

void
mii_dd_register_drives(
		mii_dd_system_t *dd,
		mii_dd_t * drives,
		uint8_t count )
{
//	printf("%s: registering %d drives\n", __func__, count);
	mii_dd_t * last = dd->drive;
	while (last && last->next)
		last = last->next;
	for (int i = 0; i < count; i++) {
		mii_dd_t *d = &drives[i];
		d->dd = dd;
		d->next = NULL;
		if (last)
			last->next = d;
		else
			dd->drive = d;
		last = d;
	}
}

void
mii_dd_file_dispose(
		mii_dd_system_t *dd,
		mii_dd_file_t *file )
{
	// remove it from dd's file queue
	if (dd->file == file)
		dd->file = file->next;
	else {
		mii_dd_file_t *f = dd->file;
		while (f) {
			if (f->next == file) {
				f->next = file->next;
				break;
			}
			f = f->next;
		}
	}
	if (file->dd) {
		file->dd->file = NULL;
		file->dd = NULL;
	}
	if (file->fd >= 0) {
		close(file->fd);
		file->fd = -1;
		file->map = NULL;
	}
	if (file->map) {
		free(file->map);
		file->map = NULL;
	}
	if (file->pathname) {
		free(file->pathname);
		file->pathname = NULL;
	}
	free(file);
}

int
mii_dd_drive_load(
		mii_dd_t *dd,
		mii_dd_file_t *file )
{
	if (dd->file == file)
		return 0;
	if (dd->file) {
		printf("%s: %s unloading %s\n", __func__,
				dd->name,
				dd->file->pathname);
		mii_dd_file_dispose(dd->dd, dd->file);
		dd->file = NULL;
	}
	if (!file)
		return 0;
	dd->file = file;
	printf("%s: %s loading %s\n", __func__,
				dd->name, file->pathname);
	if (dd->ro || dd->wp)
		return 0;
	if (mii_dd_overlay_load(dd) < 0) {
		printf("%s: No overlay to load, we're fine for now\n", __func__);
		// no overlay.. what to do?
	}
	return 0;
}

mii_dd_file_t *
mii_dd_file_load(
		mii_dd_system_t *dd,
		const char *pathname,
		uint16_t flags)
{
	if (!flags)
		flags = O_RDONLY;
	if (flags & O_WRONLY)
		flags |= O_RDWR;
    int err;
    int fd = open(pathname, flags);
    if (fd < 0) {
		printf("%s: %s: Retrying Read only\n", __func__, pathname);
		if (flags & (O_RDWR | O_WRONLY)) {
			flags &= ~(O_RDWR | O_WRONLY);
			flags |= O_RDONLY;
			fd = open(pathname, flags, 0666);
		}
	}
	if (fd < 0) {
		printf("%s: %s: Failed to open: %s\n",
				__func__, pathname, strerror(errno));
        return NULL;
    }
    struct stat st;
    err = fstat(fd, &st);
    if (err < 0) {
		perror(pathname);
        goto bail;
    }
    int protect = PROT_READ;
    int mflags = MAP_PRIVATE;
    if (flags & (O_RDWR | O_WRONLY)) {
        protect |= PROT_WRITE;
        mflags = MAP_SHARED;
    }
    uint8_t *buf = mmap(NULL, st.st_size, protect, mflags, fd, 0);
    if (buf == NULL || buf == MAP_FAILED) {
		perror(pathname);
    //    err = errno;
        goto bail;
    }
	mii_dd_file_t * res = calloc(1, sizeof(*res));
	res->pathname = strdup(pathname);
	res->fd 	= fd;
	res->map 	= buf;
	res->start	= buf;
	res->size 	= st.st_size;
	res->dd 	= NULL;
	res->next 	= dd->file;
	dd->file 	= res;
	res->read_only = (flags & O_RDWR) == 0;
	char *suffix = strrchr(pathname, '.');
	if (suffix) {
		if (!strcasecmp(suffix, ".dsk")) {
			res->format = MII_DD_FILE_DSK;
		} else if (!strcasecmp(suffix, ".po") || !strcasecmp(suffix, ".hdv")) {
			res->format = MII_DD_FILE_PO;
		} else if (!strcasecmp(suffix, ".nib")) {
			res->format = MII_DD_FILE_NIB;
		} else if (!strcasecmp(suffix, ".do")) {
			res->format = MII_DD_FILE_DO;
		} else if (!strcasecmp(suffix, ".woz")) {
			res->format = MII_DD_FILE_WOZ;
		} else if (!strcasecmp(suffix, ".2mg")) {
			res->format = MII_DD_FILE_2MG;
			res->map += 64;
		}
		printf("%s: suffix %s, format %d\n", __func__, suffix, res->format);
	}
	return res;
bail:
    close(fd);
    return NULL;
}

mii_dd_file_t *
mii_dd_file_in_ram(
		mii_dd_system_t *dd,
		const char *pathname,
		uint32_t size,
		uint16_t flags)
{
	mii_dd_file_t * res = calloc(1, sizeof(*res));
	res->pathname = strdup(pathname);
	res->fd 	= -1;
	res->map 	= calloc(1, size);
	res->start	= res->map;
	res->size 	= size;
	res->dd 	= NULL;
	res->next 	= dd->file;
	dd->file 	= res;
	res->format = MII_DD_FILE_RAM;
	return res;
}

int
mii_dd_overlay_load(
		mii_dd_t * dd )
{
	if (dd->overlay.file)
		return 0;
	if (!dd->file)
		return -1;
	// no overlay for PO disk images (floppy)
	if (!(dd->file->format == MII_DD_FILE_PO &&
			dd->file->size != 143360))
		return -1;

	char *filename = NULL;
	char *suffix = strrchr(dd->file->pathname, '.');
	if (suffix) {
		asprintf(&filename, "%.*s.miov", (int)(suffix - dd->file->pathname), dd->file->pathname);
	} else {
		asprintf(&filename, "%s.miov", dd->file->pathname);
	}
	int fd = open(filename, O_RDWR, 0666);
	if (fd == -1) {
		printf("%s: overlay %s: %s\n", __func__,
				filename, strerror(errno));
		free(filename);
		return -1;
	}
	mii_dd_file_t * file = mii_dd_file_load(dd->dd, filename, O_RDWR);
	close(fd);
	if (!file)
		return -1;
	mii_dd_overlay_header_t * h = (mii_dd_overlay_header_t *)file->start;

	if (h->magic != FCC('M','I','O','V')) {
		printf("Overlay file %s has invalid magic\n", filename);
		mii_dd_file_dispose(dd->dd, file);
		return -1;
	}
	if (h->version != 1) {
		printf("Overlay file %s has invalid version\n", filename);
		mii_dd_file_dispose(dd->dd, file);
		return -1;
	}
	if (h->size != dd->file->size / 512) {
		printf("Overlay file %s has invalid size\n", filename);
		mii_dd_file_dispose(dd->dd, file);
		return -1;
	}

	MD5_CTX d5 = {};
	MD5_Init(&d5);
	MD5_Update(&d5, dd->file->start, dd->file->size);
	uint8_t md5[16];
	MD5_Final(md5, &d5);

	if (memcmp(md5, h->src_md5, 16)) {
		printf("Overlay file %s has mismatched HASH!\n", filename);
		mii_dd_file_dispose(dd->dd, file);
		return -1;
	}
	uint32_t bitmap_size = (h->size + 63) / 64;
	dd->overlay.file = file;
	dd->overlay.file->map += sizeof(*h) + bitmap_size;
	dd->overlay.header = (mii_dd_overlay_header_t *)dd->overlay.file->start;
	dd->overlay.bitmap = (uint64_t*)(dd->overlay.file->start + sizeof(*h));
	dd->overlay.blocks = dd->overlay.file->map;

	return 0;
}

int
mii_dd_overlay_prepare(
		mii_dd_t *	dd )
{
	if (dd->overlay.file)
		return 0;
	if (!dd->file)
		return -1;
	// no overlay for PO disk images (floppy)
	if (!(dd->file->format == MII_DD_FILE_PO &&
			dd->file->size != 143360))
		return 0;
	printf("%s: %s Preparing Overlay file\n", __func__, dd->name);
	uint32_t src_blocks = dd->file->size / 512;
	uint32_t bitmap_size = (src_blocks + 63) / 64;
	uint32_t blocks_size = src_blocks * 512;
	uint32_t size = sizeof(mii_dd_overlay_header_t) + bitmap_size + blocks_size;

	char *filename = NULL;
	char *suffix = strrchr(dd->file->pathname, '.');
	if (suffix) {
		asprintf(&filename, "%.*s.miov", (int)(suffix - dd->file->pathname), dd->file->pathname);
	} else {
		asprintf(&filename, "%s.miov", dd->file->pathname);
	}
	int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd == -1) {
		printf("%s: Failed to create overlay file %s: %s\n", __func__,
				filename, strerror(errno));
		printf("%s: Allocating a RAM one, lost on quit!\n", __func__);
		dd->overlay.file = mii_dd_file_in_ram(dd->dd, filename, size, O_RDWR);
	} else {
		ftruncate(fd, size);
		dd->overlay.file = mii_dd_file_load(dd->dd, filename, O_RDWR);
	}
	if (fd != -1)
		close(fd);
	free(filename);
	mii_dd_overlay_header_t h = {
		.magic =  FCC('M','I','O','V'),
		.version = 1,
		.size = src_blocks,
	};
	// hash the whole of the file, including header
	MD5_CTX d5 = {};
	MD5_Init(&d5);
	MD5_Update(&d5, dd->file->start, dd->file->size);
	MD5_Final(h.src_md5, &d5);
	*((mii_dd_overlay_header_t *)dd->overlay.file->start) = h;
	dd->overlay.file->map += sizeof(h) + bitmap_size;
	dd->overlay.header = (mii_dd_overlay_header_t *)dd->overlay.file->start;
	dd->overlay.bitmap = (uint64_t*)(dd->overlay.file->start + sizeof(h));
	dd->overlay.blocks = dd->overlay.file->map;
	return 0;
}

int
mii_dd_read(
		mii_dd_t *	dd,
		struct mii_bank_t *bank,
		uint16_t 	addr,
		uint32_t	blk,
		uint16_t 	blockcount)
{
	if (!dd || !dd->file || !dd->file->map)
		return -1;

//	printf("%s: %s read %d blocks at %d\n",
//		__func__, dd->name, blockcount, blk);
	if (dd->overlay.file) {
		uint64_t *bitmap = dd->overlay.bitmap;
		for (int i = 0; i < blockcount; i++) {
			uint32_t b = blk + i;
//			printf("   overlay block %4d : %016llx\n", b,
//					(unsigned long long)be64toh(bitmap[b/64]));
			if (b >= dd->overlay.header->size)
				break;
			if (be64toh(bitmap[b / 64]) & (1ULL << (63 - (b & 63)))) {
//				printf("%s: reading %4d from overlay\n", __func__, b);
				mii_bank_write( bank,
						addr + (i * 512),
						dd->overlay.blocks + (b * 512),
						512);
			} else {
				mii_bank_write( bank,
						addr + (i * 512),
						dd->file->map + (b * 512),
						512);
			}
		}
	} else {
		mii_bank_write(
				bank,
				addr, dd->file->map + blk * 512,
				blockcount * 512);
	}
	return 0;
}

int
mii_dd_write(
		mii_dd_t *	dd,
		struct mii_bank_t *bank,
		uint16_t 	addr,
		uint32_t	blk,
		uint16_t 	blockcount)
{
	if (!dd || !dd->file || !dd->file->map)
		return -1;
	if (dd->ro || dd->wp)
		return -1;
//	printf("%s: %s write %d blocks at %d\n",
//		__func__, dd->name, blockcount, blk);
	mii_dd_overlay_prepare(dd);
	if (dd->overlay.file) {
		uint64_t *bitmap = dd->overlay.bitmap;
		for (int i = 0; i < blockcount; i++) {
			uint32_t b = blk + i;
			if (b >= dd->overlay.header->size)
				break;
			bitmap[b / 64] = htobe64(
					be64toh(bitmap[b/64]) | (1ULL << (63 - (b & 63))));
//			printf("%s: writing %d to overlay map: %016llx\n", __func__, b,
//					(unsigned long long)be64toh(bitmap[b/64]));
		}
		mii_bank_read(
				bank,
				addr, dd->overlay.blocks + blk * 512,
				blockcount * 512);
	} else {
		mii_bank_read(
				bank,
				addr, dd->file->map + blk * 512,
				blockcount * 512);
	}
	return 0;
}

