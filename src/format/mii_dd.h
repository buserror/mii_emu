/*
 * mii_dd.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

struct mii_dd_t;

enum {
//	MII_DD_FILE_OVERLAY = 1,
	MII_DD_FILE_RAM = 1,
	MII_DD_FILE_ROM,
	MII_DD_FILE_PO,
	MII_DD_FILE_2MG,
	MII_DD_FILE_DSK,
	MII_DD_FILE_DO,
	MII_DD_FILE_NIB,
	MII_DD_FILE_WOZ
};

// a disk image file (or chunck of ram, if ramdisk is used)
typedef struct mii_dd_file_t {
	struct mii_dd_file_t *next;
	char * 			pathname;
	uint8_t 		format;
	uint8_t 		read_only;
	uint8_t * 		start; 	// start of the file
	uint8_t * 		map;	// start of the blocks

	int 			fd;	// if fd >= 0, map is mmaped, otherwise it's malloced
	uint32_t 		size;
	struct mii_dd_t * dd;
} mii_dd_file_t;

/*
 * Overlays are made to provide what looks like read/write on files, but
 * without commiting any of the changes to the real primary file, instead
 * alld the changed blocks are kept into a sparse file and reloaded when
 * the 'real' block is read.
 * That way you can keep your disk images fresh and clean, while having
 * multiple version of them if you like.
 */
typedef union mii_dd_overlay_header_t {
	struct {
		uint32_t 		magic;			// 'MIOV'
		uint32_t 		version;		// 1 for now
		uint32_t 		flags;			// unused for now
		uint32_t 		size;			// size in blocks of original file
		uint8_t	 		src_md5[16];	// md5 of the SOURCE disk
	};
	uint32_t 		raw[16];
} mii_dd_overlay_header_t;

typedef struct mii_dd_overlay_t {
	mii_dd_overlay_header_t *header;	// points to the file mapped in memory
	uint64_t *				bitmap;				// usage bitmap
	uint8_t * 				blocks;				// raw block data
	mii_dd_file_t	*		file;				// overlay file mapping
} mii_dd_overlay_t;

struct mii_slot_t;
struct mii_dd_system_t;
struct mii_floppy_t;

// a disk drive, with a slot, a drive number, and a file
typedef struct mii_dd_t {
	struct mii_dd_t *		next;
	struct mii_dd_system_t *dd;
	const char *			name;	// ie "Disk ][ D:2"
	struct mii_floppy_t *	floppy;	// if it's a floppy drive
	uint8_t 				slot_id : 4, drive : 4;
	struct mii_slot_t *		slot;
	unsigned int 			ro : 1, wp : 1, can_eject : 1;
	mii_dd_file_t * 		file;
	mii_dd_overlay_t		overlay;
} mii_dd_t;

typedef struct mii_dd_system_t {
	mii_dd_t *				drive;	// list of all drives on all slots
	mii_dd_file_t *			file;	// list of all open files (inc overlays)
} mii_dd_system_t;

struct mii_t;

void
mii_dd_system_init(
		struct mii_t *mii,
		mii_dd_system_t *dd );
void
mii_dd_system_dispose(
		mii_dd_system_t *dd );
/*
 * register drives with the system -- these are not allocated, they are
 * statically defined in the driver code in their own structures
 */
void
mii_dd_register_drives(
		mii_dd_system_t *dd,
		mii_dd_t * drives,
		uint8_t count );
int
mii_dd_drive_load(
		mii_dd_t *dd,
		mii_dd_file_t *file );

/*
 * unmap, close and dispose of 'file', clear it from the drive, if any
 */
void
mii_dd_file_dispose(
		mii_dd_system_t *dd,
		mii_dd_file_t *file );
mii_dd_file_t *
mii_dd_file_load(
		mii_dd_system_t *dd,
		const char *filename,
		uint16_t flags);

struct mii_bank_t;
// read blocks from blk into bank's address 'addr'
int
mii_dd_read(
		mii_dd_t *	dd,
		struct mii_bank_t *bank,
		uint16_t 	addr,
		uint32_t	blk,
		uint16_t 	blockcount);

int
mii_dd_write(
		mii_dd_t *	dd,
		struct mii_bank_t *bank,
		uint16_t 	addr,
		uint32_t	blk,
		uint16_t 	blockcount);

