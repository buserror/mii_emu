/*
 * mui_65c02.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

/*
 * This is pretty heavily dependant on the way bitfields are packed in
 * bytes, so it's not technically portable; if you have problems using a
 * strange compiler, undefine this and use the 'discrete' version below.
 */
#define MII_PACK_P

/*
 * State structure used to 'talk' to the CPU emulator.
 * It works like this:
 * mii_cpu_state_t s = { .reset = 1 };
 * do {
 * 	s = mii_cpu_run(cpu, s);
 *  if (s.w)
 * 		write_memory(s.addr, s.data);
 *  else
 * 		s.data = read_memory(s.addr);
 * } while (1);
 * s.sync 	will be 1 when we are fetching a new instruction
 * s.reset 	will be 1 when the CPU is in reset (fetching vector)
 * 			will turn to zero when done
 * You check then the cpu->cycle to know what cycle you're in the current
 * instruction.
 *
 * If you want to 'jump' to a new PC, you need to
 * s.addr = new_pc;
 * s.sync = 1;
 * this will stop the current instruction and start fetching at new_pc
 *
 */
typedef union mii_cpu_state_t  {
	struct {
		uint16_t 	addr;
		uint8_t 	data;
		uint8_t		w : 1,
					sync : 1,
					reset : 1,
					irq : 1,
					nmi : 1,
					trap : 1;
	} __attribute__((packed));
	uint32_t 		raw;
} mii_cpu_state_t ;

enum {
	MII_CPU_IRQ_NONE = 0,
	MII_CPU_IRQ_IRQ,
	MII_CPU_IRQ_NMI,
	MII_CPU_IRQ_BRK,
};

#ifndef MII_65C02_DIRECT_ACCESS
#define MII_65C02_DIRECT_ACCESS		1
#endif

#if MII_65C02_DIRECT_ACCESS
struct mii_cpu_t;
typedef mii_cpu_state_t (*mii_cpu_direct_access_cb)(
		struct mii_cpu_t *cpu,
		mii_cpu_state_t   access );
#endif

/* CPU state machine */
typedef struct mii_cpu_t {
	uint8_t 	A, X, Y, S;
	/* internal 16bit registers for fetch etc
	 * _D is the 'data' register, used for 16bit operations
	 * _P is the 'pointer' register, used for 16bit addressing
	 * This is not set hard in stone, but typically addressing modes that
	 * are 'indirect' and load from memory will set _P and read in _D
	 * so the opcode doesn't have to handle s.data at all */
	uint16_t 	_D, _P;

#ifdef MII_PACK_P
	union {
		struct {
			uint8_t 	C:1, Z:1, I:1, D:1, B:1, _R:1, V:1, N:1;
		};
		uint8_t 	P;
	}			P;
#else
	/* My experience with simavr shows that maintaining a 8 bits bitfield
	 * for a status register is a lot slower than having discrete flags
	 * and 'constructing' the matching 8 bits register when needed */
	union {
		struct {
			uint8_t 	C, Z, I, D, B, _R, V, N;
		};
		uint8_t 	P[8];
	}			P;
#endif
	uint16_t 	PC;
	uint8_t 	IR;
	uint8_t		IRQ;	// IRQ (0) or NMI (1) or BRK (2)
	uint8_t		cycle;	// for current instruction
	uint32_t 	instruction_run;	// how many instructions to run
	/* sequence of instruction that will trigger a trap flag.
	 * this is used to trigger 'call backs' to the main code
	 * typically use a pair of NOPs sequence that is unlikely to exist in
	 * real code. */
	uint16_t 	trap;
	// last 4 instructions, as a shift register, used for traps or debug
	uint32_t	ir_log;

	uint64_t 	total_cycle;
#if MII_65C02_DIRECT_ACCESS
	mii_cpu_direct_access_cb access;
	void *					access_param;	// typically struct mii_t*
#else
	/* State of the protothread for the CPU state machine (minipt.h) */
	void *		state;
#endif

#ifdef MII_TEST
	/* Debug only; Only used by the test units. */
	uint8_t * 	ram; 	// DEBUG
#endif
} mii_cpu_t;

mii_cpu_state_t
mii_cpu_init(
		mii_cpu_t *cpu );

mii_cpu_state_t
mii_cpu_run(
		mii_cpu_t *cpu,
		mii_cpu_state_t s);


#ifdef MII_PACK_P
#define MII_SET_P(_cpu, _byte) { \
			(_cpu)->P.P = (_byte & 0xEF) | 0x20; \
		}                                   // FD: to pass HARTE's test : 0x20 instead of 0x30, unset Break Bit
#define MII_GET_P(_cpu, _res) \
		(_res) = (_cpu)->P.P
#define MII_SET_P_BIT(_cpu, _bit, _val) { \
		const int __bit = _bit; \
		(_cpu)->P.P = ((_cpu)->P.P & ~(1 << __bit)) | (!!(_val) << __bit); \
	}
#define MII_GET_P_BIT(_cpu, _bit) \
		!!(((_cpu)->P.P & (1 << (_bit))))
#else
#define MII_SET_P(_cpu, _byte) { \
		const int __byte = _byte; \
		for (int _pi = 0; _pi < 8; _pi++) \
			(_cpu)->P.P[_pi] = _pi == B_B || _pi == B_X || \
						((__byte) & (1 << _pi)); \
		}
#define MII_GET_P(_cpu, _res) { \
		(_res) = 0; \
		for (int _pi = 0; _pi < 8; _pi++) \
			(_res) |= (_cpu)->P.P[_pi] << _pi; \
	}
#define MII_SET_P_BIT(_cpu, _bit, _val) { \
		(_cpu)->P.P[_bit] = _val; \
	}
#define MII_GET_P_BIT(_cpu, _bit) \
		((_cpu)->P.P[_bit])
#endif
