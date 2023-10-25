//  format/empty.c
//
//  Copyright (c) 2023 Micah John Cowan.
//  This code is licensed under the MIT license.
//  See the accompanying LICENSE file for details.

#include "mii_disk_format.h"


#define byte uint8_t

void spin(DiskFormatDesc *d, bool b)
{
}

byte read_byte(DiskFormatDesc *d)
{
    // A real disk can never send a low byte.
    // But an empty disk must never send a legitimate byte.
    return 0x00;
}

void write_byte(DiskFormatDesc *d, byte b)
{
}

void eject(DiskFormatDesc *d)
{
}

DiskFormatDesc empty_disk_desc = {
    .spin = spin,
    .read_byte = read_byte,
    .write_byte = write_byte,
    .eject = eject,
};
