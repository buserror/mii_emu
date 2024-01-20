//  format/dsk.c
//
//  Copyright (c) 2023 Micah John Cowan.
//  This code is licensed under the MIT license.
//  See the accompanying LICENSE file for details.

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <assert.h>

#include "mii_disk_format.h"

#define NIBBLE_SECTOR_SIZE  416
#define NIBBLE_TRACK_SIZE   6656
#define DSK_SECTOR_SIZE     256
#define MAX_SECTORS         16
#define VOLUME_NUMBER       254
#define DSK_TRACK_SIZE      (DSK_SECTOR_SIZE * MAX_SECTORS)

#define byte uint8_t

struct dskprivdat {
    const char *path;
    byte *realbuf;
    byte *buf;
    const byte *secmap;
    int bytenum;
    uint64_t dirty_tracks;
};
static const struct dskprivdat datinit = { 0 };

static const size_t nib_disksz = 232960;
static const size_t dsk_disksz = 143360;

//  DOS 3.3 Physical sector order (index is physical sector,
//  value is DOS sector)
const byte DO[] = {
    0x0, 0x7, 0xE, 0x6, 0xD, 0x5, 0xC, 0x4,
    0xB, 0x3, 0xA, 0x2, 0x9, 0x1, 0x8, 0xF
};

// ProDOS Physical sector order (index is physical sector,
// value is ProDOS sector).
const byte PO[] = {
    0x0, 0x8, 0x1, 0x9, 0x2, 0xa, 0x3, 0xb,
    0x4, 0xc, 0x5, 0xd, 0x6, 0xe, 0x7, 0xf
};

const byte TRANS62[] = {
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6,
    0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc,
    0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde,
    0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
    0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

static const int DETRANS62[] = {
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1,   -1,   -1,   -1, 0x00, 0x01,
      -1,   -1, 0x02, 0x03,   -1, 0x04, 0x05, 0x06,
      -1,   -1,   -1,   -1,   -1,   -1, 0x07, 0x08,
      -1,   -1,   -1, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
      -1,   -1, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
      -1, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A,
      -1,   -1,   -1,   -1,   -1,   -1,   -1,   -1,
      -1,   -1,   -1, 0x1B,   -1, 0x1C, 0x1D, 0x1E,
      -1,   -1,   -1, 0x1F,   -1,   -1, 0x20, 0x21,
      -1, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28,
      -1,   -1,   -1,   -1,   -1, 0x29, 0x2A, 0x2B,
      -1, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32,
      -1,   -1, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38,
      -1, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F
};

static void realign_tracks(DiskFormatDesc *desc)
{
    /*
       When we unpack a .dsk into nibblized form, it's
       automatically aligned neatly within our track-storage
       boundaries within the disk buffer, because that's how
       we laid 'em out.

       When a sector is written or updated, it will
       tend to stay aligned, because the program had to
       find the existing sector to write to, to keep things
       in order.

       However, in the event of reformatting, there are no
       guarantees about the track being aligned conveniently.
       The formatting program can start at any old position
       and just start writing, because no existing data is going
       to be preserved.

       To deal with this, we could either:
         1) Be prepared to loop back around our track's buffer,
            mid-sector
         2) Re-align the track at our convenience, to start
            somewhere that we know for sure can't be the middle of
            sector data.

       I've opted for option (2). It would be a bad option if
       we're reading and writing a .nib file, because we'd be
       unnecessarily altering the file's structure (in cases
       where it had NOT been reformatted), but it's perfectly
       fine when we'll be discarding the nibblized format anyway.

       We handle this by seeking forward to the first sector-field
       start boundary (D5 AA 96) that we can find, and make that
       the new "start" of our track. It doesn't matter if it's a
       false-start that doesn't start a real sector-field, because one
       thing we know for *sure* is that first D5 can't be found
       in the middle of a legitimate header or data field.
    */

    struct dskprivdat *dat = desc->privdat;
    byte *buf = dat->buf;
    byte *secbuf = malloc(NIBBLE_TRACK_SIZE);
    for (int t=0; t != NUM_TRACKS; ++t) {
        byte *tstart = buf + (t * NIBBLE_TRACK_SIZE);
        byte *tend   = buf + ((t + 1) * NIBBLE_TRACK_SIZE);
        byte *talign;
        for (talign = tstart; talign <= (tend - 3); ++talign) {
            if (talign[0] == 0xD5 && talign[1] == 0xAA && talign[2] == 0x96) {
                if (talign == tstart) {
                    // Nothing to do, already aligned.
                } else {
                    size_t rollsz = talign - tstart;
                    memcpy(secbuf, tstart, rollsz);
                    memmove(tstart, talign, tend-talign);
                    memcpy(tend - rollsz, secbuf, rollsz);
                }
                break; // handle next track
            }
        }
    }
    free(secbuf);
}

static void implodeDo(DiskFormatDesc *desc)
{
    struct dskprivdat *dat = desc->privdat;

    realign_tracks(desc);

    const byte *rd = dat->buf; // nibble buf
    const byte *end = dat->buf + nib_disksz;

    bool warned = false;

    for (;;) {
        // Scan forward for a sector header
        const int sector_hdr_sz = 11; // counts prologue, but not epilogue
        for (;;) {
            // This is the only place we're "allowed" to end processing.
            if (rd >= (end - sector_hdr_sz)) goto done;
            if (rd[0] == 0xD5 && rd[1] == 0xAA && rd[2] == 0x96) break;
            ++rd;
        }

header:
        rd += 3;
        int v = ((rd[0] << 1) | 0x1) & rd[1];
        rd += 2;
        int t = ((rd[0] << 1) | 0x1) & rd[1];
        rd += 2;
        int s = ((rd[0] << 1) | 0x1) & rd[1];
        rd += 2;
        int checkSum = ((rd[0] << 1) | 0x1) & rd[1];
        rd += 2;

        if (checkSum != (v ^ t ^ s)) {
            WARN("Sector header checksum failed, t=%d s=%d"
                 " at nibblized byte %zu.\n", t, s, rd - dat->buf);
            WARN("Probable disk corruption for %s\n", dat->path);
        }

        byte truet = (rd - dat->buf)/NIBBLE_TRACK_SIZE;
        if (t != truet) {
            WARN("Sector header lying about track number"
                 " at nibblized pos %zu\n", (size_t)(rd - dat->buf));
            WARN("  (says %d but we're on track %d). Skipping sector.\n",
                 (int)t, (int)truet);
            continue;
        }
        if (s >= MAX_SECTORS) {
            WARN("Sector header sec# too high (%d; max is %d).\n",
                 (int)s, (int)MAX_SECTORS);
            WARN("  Skipping sector.\n");
            continue;
        }

    //    const int data_field_sz = 0x15A; //counts prologue, not epilogue
        for (;;) {
            if (rd >= (end - 0x15A)) goto bail;
            if (rd[0] == 0xD5 && rd[1] == 0xAA) {
                if (rd[2] == 0x96) goto header;
                if (rd[2] == 0xAD) break;
            }
            ++rd;
        }

        rd += 3;

        // Translate sector data field
        {
            int s_ = dat->secmap[s];
            byte *data = dat->realbuf + (t * DSK_TRACK_SIZE)
                + (s_ * DSK_SECTOR_SIZE);
            byte data2[0x56];
            byte last = 0;
            int val;

            if (0) {
                WARN("Translating track %d, phys sec %d (image sec %d)\n",
                     t, s, s_);
                WARN("  starting at dsk pos %zu\n", (size_t)(data - dat->realbuf));
            }

            for (int j = 0x55; j >= 0; --j) {
                val = DETRANS62[*rd - 0x80];
                if (val == -1 && !warned) {
                    warned = true;
                    WARN("Untranslatable nibble at (nibblized) pos %zu,"
                         " disk %s.\n", (size_t)(rd - dat->buf), dat->path);
                    if (rd <= end - 4) {
                        WARN("%02X %02X %02X %02X [%02X] %02X %02X %02X\n",
                             rd[-4], rd[-3], rd[-2], rd[-1],
                             rd[0], rd[1], rd[2], rd[3]);
                    }
                    WARN("CORRUPT DISK SAVE TO %s\n", dat->path);
                }
                val ^= last;
                data2[j] = val;
                last = val;
                ++rd;
            }

            for (int j = 0; j < 0x100; ++j) {
                val = DETRANS62[*rd - 0x80];
                if (val == -1 && !warned) {
                    warned = true;
                    WARN("Untranslatable nibble at t=%d, phys s=%d,"
                         " disk %s.\n", t, s, dat->path);
                    WARN("CORRUPT DISK SAVE TO %s\n", dat->path);
                }
                val ^= last;
                data[j] = val;
                last = val;
                ++rd;
            }

            int checkSum = DETRANS62[*rd++ - 0x80];
            if (checkSum != -1) checkSum ^= last;
            if (checkSum != 0) {
                WARN("Bad sector data checksum at t=%d, phys s=%d,"
                     " disk %s.\n", t, s, dat->path);
            }

            for (int k = 0, j = 0x55; k < 0x100; ++k) {
                data[k] <<= 1;
                if ((data2[j] & 0x1) != 0) {
                    data[k] |= 0x01;
                }
                data2[j] >>= 1;

                data[k] <<= 1;
                if ((data2[j] & 0x01) != 0) {
                    data[k] |= 0x01;
                }
                data2[j] >>= 0x01;

                if (--j < 0) j = 0x55;
            }
        }
    }

bail:
    WARN("Error translating to dsk: ended mid-sector!\n");
    WARN("Probable disk corruption for %s\n", dat->path);
done:
    return;
}

static void spin(DiskFormatDesc *desc, bool b)
{
    struct dskprivdat *dat = desc->privdat;
    if (!b && dat->dirty_tracks != 0) {
        implodeDo(desc);
        // For now, sync the entire disk
        errno = 0;
        int err = msync(dat->realbuf, dsk_disksz, MS_SYNC);
        if (err < 0) {
            DIE(1,"Couldn't sync to disk file %s: %s\n",
                dat->path, strerror(errno));
        }
        dat->dirty_tracks = 0;
    }
}

static byte read_byte(DiskFormatDesc *desc)
{
    struct dskprivdat *dat = desc->privdat;
    size_t pos = (desc->halftrack/2) * NIBBLE_TRACK_SIZE;
    pos += (dat->bytenum % NIBBLE_TRACK_SIZE);
    byte val = dat->buf[pos];
    dat->bytenum = (dat->bytenum + 1) % NIBBLE_TRACK_SIZE;
    return val;
}

static void write_byte(DiskFormatDesc *desc, byte val)
{
    struct dskprivdat *dat = desc->privdat;
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
    struct dskprivdat *dat = desc->privdat;
    (void) munmap(dat->buf, dsk_disksz);
    free((void*)dat->path);
    free(dat);
}

// This function is derived from Scullin Steel Co.'s apple2js code
// https://github.com/whscullin/apple2js/blob/e280c3d/js/formats/format_utils.ts#L140
static void explodeSector(byte vol, byte track, byte sector,
                          byte **nibSec, const byte *data)
{
    byte *wr = *nibSec;
    unsigned int gap;

    // Gap 1/3 (40/0x28 bytes)

    if (sector == 0) // Gap 1
        gap = 0x80;
    else { // Gap 3
        gap = track == 0? 0x28 : 0x26;
    }

    for (uint8_t i = 0; i != gap; ++i) {
        *wr++ = 0xFF;
    }

    // Address Field
    const byte checksum = vol ^ track ^ sector;
    *wr++ = 0xD5; *wr++ = 0xAA; *wr++ = 0x96; // Address Prolog D5 AA 96
    *wr++ = (vol >> 1) | 0xAA; *wr++ = vol | 0xAA;
    *wr++ = (track >> 1)  | 0xAA; *wr++ = track  | 0xAA;
    *wr++ = (sector >> 1) | 0xAA; *wr++ = sector | 0xAA;
    *wr++ = (checksum >> 1) | 0xAA; *wr++ = checksum | 0xAA;
    *wr++ = 0xDE; *wr++ = 0xAA; *wr++ = 0xEB; // Epilogue DE AA EB

    // Gap 2 (5 bytes)
    for (int i = 0; i != 5; ++i) {
        *wr++ = 0xFF;
    }

    // Data Field
    *wr++ = 0xD5; *wr++ = 0xAA; *wr++ = 0xAD; // Data Prolog D5 AA AD

    byte *nibbles = wr;
    const unsigned ptr2 = 0;
    const unsigned ptr6 = 0x56;

    for (int i = 0; i != 0x156; ++i) {
        nibbles[i] = 0;
    }

    int i2 = 0x55;
    for (int i6 = 0x101; i6 >= 0; --i6) {
        byte val6 = data[i6 % 0x100];
        byte val2 = nibbles[ptr2 + i2];

        val2 = (val2 << 1) | (val6 & 1);
        val6 >>= 1;
        val2 = (val2 << 1) | (val6 & 1);
        val6 >>= 1;

        nibbles[ptr6 + i6] = val6;
        nibbles[ptr2 + i2] = val2;

        if (--i2 < 0)
            i2 = 0x55;
    }

    byte last = 0;
    for (int i = 0; i != 0x156; ++i) {
        const byte val = nibbles[i];
        nibbles[i] = TRANS62[last ^ val];
        last = val;
    }
    wr += 0x156; // advance write-pointer
    *wr++ = TRANS62[last];

    *wr++ = 0xDE; *wr++ = 0xAA; *wr++ = 0xEB; // Epilogue DE AA EB

    // Gap 3
    *wr++ = 0xFF;

    *nibSec = wr;
}

static void explodeDsk(byte *nibbleBuf, byte *dskBuf, const byte *secmap)
{
    for (int t = 0; t < NUM_TRACKS; ++t) {
        byte *writePtr = nibbleBuf;
        for (int phys_sector = 0; phys_sector < MAX_SECTORS; ++phys_sector) {
            const byte dos_sector = secmap[phys_sector];
            const size_t off = ((MAX_SECTORS * t + dos_sector)
                                * DSK_SECTOR_SIZE);
            explodeSector(VOLUME_NUMBER, t, phys_sector,
                          &writePtr, &dskBuf[off]);
        }
        assert(writePtr - nibbleBuf <= NIBBLE_TRACK_SIZE);
        for (; writePtr != (nibbleBuf + NIBBLE_TRACK_SIZE); ++writePtr) {
            *writePtr = 0xFF;
        }
        nibbleBuf += NIBBLE_TRACK_SIZE;
    }
}

DiskFormatDesc dsk_insert(const char *path, byte *buf, size_t sz)
{
    if (sz != dsk_disksz) {
        DIE(0,"Wrong disk image size for %s:\n", path);
        DIE(1,"  Expected %zu, got %zu.\n", dsk_disksz, sz);
    }

    struct dskprivdat *dat = malloc(sizeof *dat);
    *dat = datinit;
    dat->realbuf = buf;
    dat->path = strdup(path);
    dat->buf = calloc(1, nib_disksz);

    const char *ext = rindex(path, '.');
    ext = ext ? ext+1 : "";
    if (!strcasecmp(ext, "PO"))  {
        INFO("Opening %s as PO.\n", dat->path);
        dat->secmap = PO;
    } else {
        INFO("Opening %s as DO.\n", dat->path);
        dat->secmap = DO;
    }
    explodeDsk(dat->buf, dat->realbuf, dat->secmap);

    return (DiskFormatDesc){
        .privdat = dat,
        .spin = spin,
        .read_byte = read_byte,
        .write_byte = write_byte,
        .eject = eject,
    };
}
