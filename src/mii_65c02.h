#pragma once

#include <stdint.h>

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
typedef union mii_cpu_state_t {
	struct {
		uint32_t	addr : 16,
					data : 8,
					w : 1,
					sync : 1,
					reset : 1,
					irq : 1,
					nmi : 1,
					trap : 1;
	};
	uint32_t 		raw;
} mii_cpu_state_t;

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
	/* My experience with simavr shows that maintaining a 8 bits bitfield
	 * for a status register is a lot slower than having discrete flags
	 * and 'constructing' the matching 8 biots register when needed */
	union {
		struct {
			uint8_t 	C, Z, I, D, B, _R, V, N;
		};
		uint8_t 	P[8];
	}			P;
	uint16_t 	PC;
	uint8_t 	IR;
	uint8_t		IRQ;	// IRQ (0) or NMI (1) or BRK (2)
	uint8_t		cycle;	// for current instruction
	/* State of the CPU state machine */
	void *		state;

	/* sequence of instruction that will trigger a trap flag.
	 * this is used to trigger 'call backs' to the main code
	 * typically use a pair of NOPs sequence that is unlikely to exist in
	 * real code. */
	uint16_t 	trap;
	// last 4 instructions, as a shift register, used for traps or debug
	uint32_t	ir_log;

	/* Debug only; the callback is called every cycles, with the current
	 * state of the cpu. */
	uint8_t * 	ram; 	// DEBUG
} mii_cpu_t;

mii_cpu_state_t
mii_cpu_init(
		mii_cpu_t *cpu );

mii_cpu_state_t
mii_cpu_run(
		mii_cpu_t *cpu,
		mii_cpu_state_t s);
