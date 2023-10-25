//  format/nib.c
//
//  Copyright (c) 2023 Micah John Cowan.
//  This code is licensed under the MIT license.
//  See the accompanying LICENSE file for details.

#include "mii_disk_format.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

#define byte uint8_t

#define NIBBLE_TRACK_SIZE   6656
#define NIBBLE_SECTOR_SIZE  416
#define MAX_SECTORS         16

struct nibprivdat {
    const char *path;
    byte *buf;
    int bytenum;
    uint64_t dirty_tracks;
};
static const struct nibprivdat datinit = { 0 };

static const size_t nib_disksz = 232960;

static void spin(DiskFormatDesc *desc, bool b)
{
    struct nibprivdat *dat = desc->privdat;
    if (!b && dat->dirty_tracks != 0) {
        // For now, sync the entire disk
        errno = 0;
        int err = msync(dat->buf, nib_disksz, MS_SYNC);
        if (err < 0) {
            DIE(1,"Couldn't sync to disk file %s: %s\n",
                dat->path, strerror(errno));
        }
        dat->dirty_tracks = 0;
    }
}

static byte read_byte(DiskFormatDesc *desc)
{
    struct nibprivdat *dat = desc->privdat;
    size_t pos = (desc->halftrack/2) * NIBBLE_TRACK_SIZE;
    pos += (dat->bytenum % NIBBLE_TRACK_SIZE);
    byte val = dat->buf[pos];
    dat->bytenum = (dat->bytenum + 1) % NIBBLE_TRACK_SIZE;
    return val;
}

static void write_byte(DiskFormatDesc *desc, byte val)
{
    struct nibprivdat *dat = desc->privdat;
    if ((val & 0x80) == 0) {
        // D2DBG("dodged write $%02X", val);
        return; // must have high bit
    }
    dat->dirty_tracks |= 1 << (desc->halftrack/2);
    size_t pos = (desc->halftrack/2) * NIBBLE_TRACK_SIZE;
    pos += (dat->bytenum % NIBBLE_TRACK_SIZE);

    //D2DBG("write byte $%02X at pos $%04zX", (unsigned int)val, pos);

    dat->buf[pos] = val;
    dat->bytenum = (dat->bytenum + 1) % NIBBLE_TRACK_SIZE;
}

static void eject(DiskFormatDesc *desc)
{
    // free dat->path and dat, and unmap disk image
    struct nibprivdat *dat = desc->privdat;
    (void) munmap(dat->buf, nib_disksz);
    free((void*)dat->path);
    free(dat);
}

DiskFormatDesc nib_insert(const char *path, byte *buf, size_t sz)
{
    if (sz != nib_disksz) {
        DIE(0,"Wrong disk image size for %s:\n", path);
        DIE(1,"  Expected %zu, got %zu.\n", nib_disksz, sz);
    }

    struct nibprivdat *dat = malloc(sizeof *dat);
    *dat = datinit;
    dat->buf = buf;
    dat->path = strdup(path);

    return (DiskFormatDesc){
        .privdat = dat,
        .spin = spin,
        .read_byte = read_byte,
        .write_byte = write_byte,
        .eject = eject,
    };
}
