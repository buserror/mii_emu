/*
 * mockingboard.h
 * This is a straigth derivative of Clemens IIgs emulator mockingboard
 * emulation code. The original code is available at:
 * https://github.com/samkusin/clemens_iigs
 *
 * The original code is also licensed under the MIT License.
 * SPDX-License-Identifier: MIT
 *
 */
#pragma once
#include <stdint.h>

typedef uint64_t mb_clocks_time_t;
typedef uint32_t mb_clocks_t;

/* Typically this is passed around as the current time for the machine
	and is guaranteed to be shared between machine and any external cards
	based on the ref_step.

	For example, MMIO clocks use the Mega2 reference step
*/
typedef struct mb_clock_t {
	mb_clocks_time_t ts;
	mb_clocks_t		 ref_step;
} mb_clock_t;

struct mb_t;

/** A bit confusing and created to avoid floating point math whenever possible
 *  (whether this was a good choice given modern architectures... ?)
 *
 *  Used for calculating our system clock.  These values are relative to each other.
 *
 *  The clocks per mega2 cycle (PHI0) value will always be the largest.
 *  Yet since most time calculations in the emulator are done with fixed point-like
 *  math, the aim is to keep the clocks count per cycle high enough for fixed
 *  math to work with uint 32-bit numbers.
 *
 *  SO DON'T CHANGE THESE UNLESS ALL DEPENDENT DEFINE/CALCULATIONS THAT THESE
 *  VALUES TRICKLE DOWN REMAIN VALID.   IF YOU DO, TEST *EVERYTHING* (IWM,
 * DIAGNOSTICS)
 *
 *  Based on this, care should be taken when attempting to emulate a 8mhz machine
 *  in the future - though most I/O is performed using PHI0 cycles.
 *
 *  If you divide the MB_CLOCKS_PHI0_CYCLE by the MB_CLOCKS_PHI2_FAST_CYCLE
 *  the value will be the effective maximum clock speed in Mhz of the CPU.
 *
 *  Ref: https://www.kansasfest.org/wp-content/uploads/2011-krue-fpi.pdf
 */

#define MB_CLOCKS_14MHZ_CYCLE	   200U							// 14.318 Mhz
#define MB_CLOCKS_PHI0_CYCLE	   (MB_CLOCKS_14MHZ_CYCLE * 14) // 1.023  Mhz with stretch

// IMPORTANT! This is rounded from 69.8ns - which when scaling up to PHI0 translates
//            to 977.7 ns without rounding.  Due to the stretch cycle, this
//            effectively rounds up to 980ns, which is really what most system
//            timings rely on So, rounding up.  Bonne chance.
#define MB_14MHZ_CYCLE_NS		   70U

#define MB_MEGA2_CYCLES_PER_SECOND 1023000U
/* these are here for references - the actual functions are determined
	by which bits in the address register are set on io_read and io_write
*/
#define MB_CARD_MOCKINGBOARD_ORB1  0x00
#define MB_CARD_MOCKINGBOARD_ORA1  0x01
#define MB_CARD_MOCKINGBOARD_DDRB1 0x02
#define MB_CARD_MOCKINGBOARD_DDRA1 0x03
#define MB_CARD_MOCKINGBOARD_ORB2  0x80
#define MB_CARD_MOCKINGBOARD_ORA2  0x81
#define MB_CARD_MOCKINGBOARD_DDRB2 0x82
#define MB_CARD_MOCKINGBOARD_DDRA2 0x83

/** All mmio memory operations can have this option - both onboard and
 *  card operations
 */
#define MB_OP_IO_NO_OP			   0x01
#define MB_IS_IO_NO_OP(_flags_)	   (((_flags_) & MB_OP_IO_NO_OP) != 0)

//#define MB_OP_IO_CARD			   0x40
//#define MB_OP_IO_DEVSEL			   0x80

#define MB_CARD_IRQ				   0x80000000
#define MB_CARD_NMI				   0x40000000

//
/** _clock_ is of type mb_clock_t
	 BEWARE - these macros act on sub second time intervals (per frame deltas.)
	 Do not use these utilities to calculate values over long time intervals
*/
#define mb_ns_step_from_clocks(_clocks_step_) \
	((uint)(MB_14MHZ_CYCLE_NS * (_clocks_step_) / MB_CLOCKS_14MHZ_CYCLE))

#define mb_clocks_step_from_ns(_ns_) \
	((mb_clocks_t)((_ns_) * MB_CLOCKS_14MHZ_CYCLE) / MB_14MHZ_CYCLE_NS)

/* intentional - paranthesized expression should be done first to avoid precision
 * loss*/
#define mb_secs_from_clocks(_clock_)                                           \
	((MB_14MHZ_CYCLE_NS * (uint64_t)((_clock_)->ts / MB_CLOCKS_14MHZ_CYCLE)) * \
	 1.0e-9)

#ifdef __cplusplus
extern "C" {
#endif

struct mb_t *
mb_alloc();
void
mb_dispose( //
	struct mb_t * mb);
void
mb_io_read( //
	struct mb_t *board,
	uint8_t		*data,
	uint8_t		 addr);
void
mb_io_write( //
	struct mb_t *board,
	uint8_t		 data,
	uint8_t		 addr);
uint32_t
mb_io_sync( //
	struct mb_t *board,
	mb_clock_t	*clock);
void
mb_io_reset( //
	struct mb_t *board,
	mb_clock_t	*clock);

uint
mb_ay3_render( //
	struct mb_t * mb,
	float		*samples_out,
	uint		 sample_limit,
	uint		 samples_per_frame,
	uint		 samples_per_second);

#ifdef __cplusplus
}
#endif
