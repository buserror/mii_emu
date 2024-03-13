/*
 * mii_vcd.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
/*
 * Value change dump (VCD) file format generator for debug purpose
 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>

#include "fifo_declare.h"

#define MII_VCD_MAX_SIGNALS 64

struct mii_signal_t;
struct mii_signal_pool_t;

/*!
 * Public SIGNAL structure
 */
typedef struct mii_signal_t {
	struct mii_signal_pool_t *	pool;
	const char * 		name;
	uint32_t			sig;			//!< any value the user needs
	uint32_t			value;			//!< current value
	uint8_t				flags;			//!< SIG_* flags
	struct mii_signal_hook_t * hook;	//!< list of hooks to be notified
} mii_signal_t;


typedef struct mii_vcd_signal_t {
	/*
	 * For VCD output this is the IRQ we receive new values from.
	 */
	mii_signal_t 		sig;
	char 				alias;			// vcd one character alias
	uint8_t				size;			// in bits
	char 				name[32];		// full human name
} mii_vcd_signal_t, *mii_vcd_signal_p;

typedef struct mii_vcd_log_t {
	uint64_t 			when;			// Cycles for output,
										//     nS for input.
	uint64_t			sigindex : 8,	// index in signal table
						floating : 1,
						value : 32;
} mii_vcd_log_t, *mii_vcd_log_p;

DECLARE_FIFO(mii_vcd_log_t, mii_vcd_fifo, 256);

typedef struct mii_vcd_t {
	struct mii_t *		mii;
	char *				filename;		// .vcd filename
	FILE * 				output;

	int 				signal_count;
	mii_vcd_signal_t	signal[MII_VCD_MAX_SIGNALS];

	uint64_t 			cycle;
	uint64_t 			start;
	uint64_t 			cycle_to_nsec;		// for output cycles
	uint64_t 			vcd_to_ns;	// for input unit mapping

	mii_vcd_fifo_t		log;
} mii_vcd_t;


// initializes a new VCD trace file, and returns zero if all is well
int
mii_vcd_init(
		struct mii_t * mii,
		const char * filename, 	// filename to write
		mii_vcd_t * vcd,		// vcd struct to initialize
		uint32_t	cycle_to_nsec );	// 1000 for 1Mhz
int
mii_vcd_init_input(
		struct mii_t * mii,
		const char * filename, 	// filename to read
		mii_vcd_t * vcd );		// vcd struct to initialize
void
mii_vcd_close(
		mii_vcd_t * vcd );

// Add a trace signal to the vcd file. Must be called before mii_vcd_start()
int
mii_vcd_add_signal(
		mii_vcd_t * vcd,
		mii_signal_t * signal_sig,
		uint signal_bit_size,
		const char * name );

// Starts recording the signal value into the file
int
mii_vcd_start(
		mii_vcd_t * vcd);
// stops recording signal values into the file
int
mii_vcd_stop(
		mii_vcd_t * vcd);




/*
 * Internal IRQ system
 *
 * This subsystem allows any piece of code to "register" a hook to be called when an IRQ is
 * raised. The IRQ definition is up to the module defining it, for example a IOPORT pin change
 * might be an IRQ in which case any piece of code can be notified when a pin has changed state
 *
 * The notify hooks are chained, and duplicates are filtered out so you can't register a
 * notify hook twice on one particular IRQ
 *
 * IRQ calling order is not defined, so don't rely on it.
 *
 * IRQ hook needs to be registered in reset() handlers, ie after all modules init() bits
 * have been called, to prevent race condition of the initialization order.
 */
struct mii_signal_t;

typedef void (*mii_signal_notify_t)(
		struct mii_signal_t * sig,
		uint32_t value,
		void * param);


enum {
	SIG_FLAG_NOT		= (1 << 0),	//!< change polarity of the IRQ
	SIG_FLAG_FILTERED	= (1 << 1),	//!< do not "notify" if "value" is the same as previous raise
	SIG_FLAG_ALLOC		= (1 << 2), //!< this sig structure was malloced via mii_alloc_sig
	SIG_FLAG_INIT		= (1 << 3), //!< this sig hasn't been used yet
	SIG_FLAG_FLOATING	= (1 << 4), //!< this 'pin'/signal is floating
	SIG_FLAG_USER		= (1 << 5), //!< Can be used by sig users
};

/*
 * IRQ Pool structure
 */
typedef struct mii_signal_pool_t {
	uint count;						//!< number of sigs living in the pool
	struct mii_signal_t ** sig;		//!< sigs belonging in this pool
} mii_signal_pool_t;

//! allocates 'count' IRQs, initializes their "sig" starting from 'base'
// and increment
mii_signal_t *
mii_alloc_signal(
		mii_signal_pool_t * pool,
		uint32_t base,
		uint32_t count,
		const char ** names /* optional */);
void
mii_free_signal(
		mii_signal_t * sig,
		uint32_t count);

//! init 'count' IRQs, initializes their "sig" starting from 'base' and increment
void
mii_init_signal(
		mii_signal_pool_t * pool,
		mii_signal_t * sig,
		uint32_t base,
		uint32_t count,
		const char ** names /* optional */);
//! Returns the current IRQ flags
uint8_t
mii_signal_get_flags(
		mii_signal_t * sig );
//! Sets this sig's flags
void
mii_signal_set_flags(
		mii_signal_t * sig,
		uint8_t flags );
//! 'raise' an IRQ. Ie call their 'hooks', and raise any chained IRQs, and set the new 'value'
void
mii_raise_signal(
		mii_signal_t * sig,
		uint32_t value);
//! Same as mii_raise_signal(), but also allow setting the float status
void
mii_raise_signal_float(
		mii_signal_t * sig,
		uint32_t value,
		int floating);
//! this connects a "source" IRQ to a "destination" IRQ
void
mii_connect_signal(
		mii_signal_t * src,
		mii_signal_t * dst);
void
mii_unconnect_signal(
		mii_signal_t * src,
		mii_signal_t * dst);

//! register a notification 'hook' for 'sig' -- 'param' is anything that your want passed back as argument
void
mii_signal_register_notify(
		mii_signal_t * sig,
		mii_signal_notify_t notify,
		void * param);

void
mii_signal_unregister_notify(
		mii_signal_t * sig,
		mii_signal_notify_t notify,
		void * param);
