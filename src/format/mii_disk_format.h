/*
 * mii_disk_format.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/********** FORMATS  **********/
#define NUM_TRACKS      35
#define SECTOR_SIZE     256
typedef struct DiskFormatDesc DiskFormatDesc;
typedef struct DiskFormatDesc {
    void *privdat;
    bool writeprot;
    unsigned int halftrack;
    void (*spin)(DiskFormatDesc *, bool);
    uint8_t (*read_byte)(DiskFormatDesc *);
    void (*write_byte)(DiskFormatDesc *, uint8_t);
    void (*eject)(DiskFormatDesc *);
} DiskFormatDesc;

DiskFormatDesc disk_format_load(const char *path);

#define WARN(...) fprintf(stderr, __VA_ARGS__)
#define INFO(...) fprintf(stderr, __VA_ARGS__)
#define DIE(code, ...) do { \
		WARN(__VA_ARGS__); \
		if (code) exit(code); \
	} while(0)
