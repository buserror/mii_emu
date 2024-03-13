/*
 * mii_ssc.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * This is mostly a duplicate from the UI one in mii_mui_settings.h, but it is
 * part of the way we decouple the UI from the emulator, so we can test the UI
 * without having to link against the emulator.
 */
// this is to be used with MII_SLOT_SSC_SET_TTY and mii_slot_command()
typedef struct mii_ssc_setconf_t {
	unsigned int baud, bits : 4, parity : 4, stop : 4, handshake : 4,
				is_device : 1, is_socket : 1, is_pty : 1;
	unsigned socket_port;
	char device[256];
} mii_ssc_setconf_t;

