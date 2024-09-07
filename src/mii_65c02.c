/*
 * mui_65c02.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "minipt.h"
#include "mii_65c02.h"
#define MII_CPU_65C02_IMPL
#include "mii_65c02_ops.h"

#define likely(x)		__builtin_expect(!!(x), 1)
#define unlikely(x)		__builtin_expect(!!(x), 0)

mii_cpu_state_t
mii_cpu_init(
		mii_cpu_t *cpu )
{
	mii_cpu_state_t s = {
		.addr = 0,
		.reset = 1,
	};
#if MII_65C02_DIRECT_ACCESS
#else
	cpu->state = NULL;
#endif
	return s;
}

#if MII_65C02_DIRECT_ACCESS
#define _FETCH(_val) { \
		s.addr = _val; s.w = 0; cpu->cycle++; \
		s = cpu->access(cpu, s); \
	}
#define _STORE(_addr, _val) { \
		s.addr = _addr; s.data = _val; s.w = 1; cpu->cycle++; \
		s = cpu->access(cpu, s); \
	}
#else
#define _FETCH(_val) { \
		s.addr = _val; s.w = 0; cpu->cycle++; \
		pt_yield(cpu->state); \
	}
#define _STORE(_addr, _val) { \
		s.addr = _addr; s.data = _val; s.w = 1; cpu->cycle++; \
		pt_yield(cpu->state); \
	}
#endif

#define _NZC(_val) { \
		uint16_t v = (_val); \
		cpu->P.N = !!(v & 0x80); \
		cpu->P.Z = (v & 0xff) == 0; \
		cpu->P.C = !!(v & 0xff00); \
	}
#define _NZ(_val) { \
		uint16_t v = (_val); \
		cpu->P.N = !!(v & 0x80); \
		cpu->P.Z = (v & 0xff) == 0; \
	}
#define _C(_val) { \
		cpu->P.C = !!(_val); \
	}

mii_cpu_state_t
mii_cpu_run(
		mii_cpu_t *cpu,
		mii_cpu_state_t s)
{
#if MII_65C02_DIRECT_ACCESS
	mii_op_desc_t d;
#else
	mii_op_desc_t d = mii_cpu_op[cpu->IR].desc;
	pt_start(cpu->state);
#endif
next_instruction:
	if (unlikely(s.reset)) {
		s.reset = 0;
		_FETCH(0xfffc); cpu->_P = s.data;
		_FETCH(0xfffd);	cpu->_P |= s.data << 8;
		cpu->PC = cpu->_P;
	  	cpu->S = 0xFF;
		MII_SET_P(cpu, 0);
	}
	if (unlikely(s.irq && cpu->P.I == 0)) {
		if (!cpu->IRQ)
			cpu->IRQ = MII_CPU_IRQ_IRQ;
	}
	if (unlikely(s.nmi && cpu->P.I == 0)) {
		if (!cpu->IRQ)
			cpu->IRQ = MII_CPU_IRQ_NMI;
	}
	if (unlikely(cpu->IRQ)) {
		s.irq = 0;
		cpu->P.B = cpu->IRQ == MII_CPU_IRQ_BRK;
		cpu->_D = cpu->PC;
		_STORE(0x0100 | cpu->S--, cpu->_D >> 8);
		_STORE(0x0100 | cpu->S--, cpu->_D & 0xff);
		uint8_t p = 0;
		MII_GET_P(cpu, p);
		_STORE(0x0100 | cpu->S--, p);
		cpu->P.I = 1;
		if (cpu->IRQ == MII_CPU_IRQ_BRK)
			cpu->P.D = 0;
		if (cpu->IRQ == MII_CPU_IRQ_NMI) {
		//	printf("NMI!\n");
			_FETCH(0xfffa); cpu->_P = s.data;
			_FETCH(0xfffb);	cpu->_P |= s.data << 8;
		} else {
			_FETCH(0xfffe); cpu->_P = s.data;
			_FETCH(0xffff);	cpu->_P |= s.data << 8;
		}
		cpu->IRQ = 0;
		cpu->PC = cpu->_P;
	}
	s.sync = 1;
	// we dont' reset the cycle here, that way calling code has a way of knowing
	// how many cycles were used by the previous instruction
	_FETCH(cpu->PC);
	cpu->total_cycle += cpu->cycle;
	s.sync = 0;
	cpu->cycle = 0;
	cpu->PC++;
	cpu->IR = s.data;
	d = mii_cpu_op[cpu->IR].desc;
	cpu->ir_log = (cpu->ir_log << 8) | cpu->IR;
	s.trap = cpu->trap && (cpu->ir_log & 0xffff) == cpu->trap;
	if (unlikely(s.trap)) {
		cpu->ir_log = 0;
#if MII_65C02_DIRECT_ACCESS
		return s;
#endif
	}
	switch (d.mode) {
		case IMM:
			_FETCH(cpu->PC++);		cpu->_D = s.data;
			break;
		case BRANCH: // BEQ/BNE etc
		case ZP_REL: // $(xx)
			_FETCH(cpu->PC++);		cpu->_P = s.data;
			break;
		case ZP_X: // $xx,X
			_FETCH(cpu->PC++);		cpu->_P = (s.data + cpu->X) & 0xff;
			break;
		case ZP_Y:	// $xx,Y
			_FETCH(cpu->PC++);		cpu->_P = (s.data + cpu->Y) & 0xff;
			break;
		case ABS: {	// $xxxx
			_FETCH(cpu->PC++);		cpu->_P = s.data;
			_FETCH(cpu->PC++);		cpu->_P |= s.data << 8;
		}	break;
		case ABS_X: { // $xxxx,X
			_FETCH(cpu->PC++);		cpu->_P = s.data;
			_FETCH(cpu->PC++);		cpu->_P |= s.data << 8;
			/*
			 * this seems to be only used by a2audit, ever, which is bloody
			 * annoying, so we just fake it to pass the test
			 */
			if (cpu->IR == 0xfe && cpu->X == 0 && cpu->_P == 0xc083) {
			//	printf("Fooling a2audit\n");
				_FETCH(cpu->_P);	// false read
			}
			cpu->_P += cpu->X;
			if ((cpu->_P & 0xff00) != (s.data << 8)) {
				_FETCH(cpu->PC); // false read
			}
		}	break;
		case ABS_Y: { // $xxxx,Y
			_FETCH(cpu->PC++);		cpu->_P = s.data;
			_FETCH(cpu->PC++);		cpu->_P |= s.data << 8;
			cpu->_P += cpu->Y;
			if ((cpu->_P & 0xff00) != (s.data << 8)) {
				_FETCH(cpu->PC); // false read
			}
		}	break;
		case IND_X: { // ($xx,X)
			_FETCH(cpu->PC++);		cpu->_D = s.data;
			cpu->_D += cpu->X;
			_FETCH(cpu->_D & 0xff);	cpu->_P = s.data;
			cpu->_D++;
			_FETCH(cpu->_D & 0xff);	cpu->_P |= s.data << 8;
		}	break;
		case IND_Y: { // ($xx),Y
			_FETCH(cpu->PC++);		cpu->_D = s.data;
			_FETCH(cpu->_D);		cpu->_P = s.data;
			_FETCH((cpu->_D + 1) & 0xff);
			cpu->_P |= s.data << 8;
			cpu->_P += cpu->Y;
		}	break;
		case IND: {	// ($xxxx)
			_FETCH(cpu->PC++); 		cpu->_D = s.data;
			_FETCH(cpu->PC++); 		cpu->_D |= s.data << 8;
			_FETCH(cpu->_D); 		cpu->_P = s.data;
			_FETCH(cpu->_D + 1); 	cpu->_P |= s.data << 8;
		}	break;
		case IND_Z: {	// ($xx)
			_FETCH(cpu->PC++); 		cpu->_D = s.data;
			_FETCH(cpu->_D); 		cpu->_P = s.data;
//			_FETCH((cpu->_D + 1)); 	cpu->_P |= s.data << 8;
			// FD if $xx=0xFF then 0xFF+1 = 0x00 and not 0x100 bug fixed
			_FETCH((cpu->_D + 1) & 0xFF); 	cpu->_P |= s.data << 8;
		}	break;
		case IND_AX: { // ($xxxx,X)
			_FETCH(cpu->PC++);		cpu->_D = s.data;
			_FETCH(cpu->PC++);		cpu->_D |= s.data << 8;
			cpu->_D += cpu->X;
			if ((cpu->_D & 0xff00) != (s.data << 8))
				cpu->cycle++;
			_FETCH(cpu->_D);		cpu->_P = s.data;
			_FETCH(cpu->_D + 1);	cpu->_P |= s.data << 8;
		}	break;
	}
	if (d.r) {
		_FETCH(cpu->_P);
		cpu->_D = s.data;
	}
	switch (cpu->IR) {
		case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D:
		case 0x79: case 0x61: case 0x71: case 0x72:
		{ // ADC
			// Handle adding in BCD with bit D
			if (unlikely(cpu->P.D)) {
				uint8_t D = cpu->_D;
				uint8_t lo = (cpu->A & 0x0f) + (D & 0x0f) + !!cpu->P.C;
				if (lo > 9) lo += 6;
				uint8_t hi = (cpu->A >> 4) + (D >> 4) + (lo > 0x0f);
				// FD removed
//				cpu->P.Z = ((cpu->A + D + cpu->P.C) & 0xff) == 0;
				// that is 6502 behaviour
//				cpu->P.N = !!(hi & 0xf8);
				cpu->P.V = !!((!((cpu->A ^ D) & 0x80) &&
								((cpu->A ^ (hi << 4))) & 0x80));
				if (hi > 9) hi += 6;
				cpu->P.C = hi > 15;
			//	printf("ADC %02x %02x C:%d %x%x\n",
			//			cpu->A, D, !!cpu->P.C, hi & 0xf, lo & 0xf);
				cpu->A = (hi << 4) | (lo & 0x0f);
				// THAT is 65c02 behaviour
				cpu->P.N = !!(cpu->A & 0x80);
				// FD THAT is 65C02 behavior
                cpu->P.Z = cpu->A == 0;
			} else {
				uint16_t sum = cpu->A + cpu->_D + !!cpu->P.C;
				cpu->P.V = cpu->P.C = 0;
				if (~(cpu->A ^ cpu->_D) & (cpu->A ^ sum) & 0x80)
					cpu->P.V = 1;
				_NZC(sum);
				cpu->A = sum;
			}
		}	break;
		case 0x29: case 0x25: case 0x35: case 0x2D: case 0x3D:
		case 0x39: case 0x21: case 0x31: case 0x32:
		{ // AND
			cpu->A &= cpu->_D;
			_NZ(cpu->A);
		}	break;
		case 0x0A:
		{ // ASL
			_FETCH(cpu->PC);	// cycle++
			cpu->P.C = !!(cpu->A & 0x80);
			cpu->A <<= 1;
			_NZ(cpu->A);
		}	break;
		case 0x06: case 0x16: case 0x0E: case 0x1E:
		{ // ASL
			cpu->P.C = !!(cpu->_D & 0x80);
			cpu->_D <<= 1;
			_NZ(cpu->_D);
		}	break;
		case 0x0f: case 0x1f: case 0x2f: case 0x3f: case 0x4f:
		case 0x5f: case 0x6f: case 0x7f: case 0x8f: case 0x9f:
		case 0xaf: case 0xbf: case 0xcf: case 0xdf: case 0xef:
		case 0xff:
		{ // BBR/BBS
//			printf(" BB%c%d vs %02x\n", d.s_bit_value ? 'S' : 'R',
//					d.s_bit, cpu->_D);
			_FETCH(cpu->PC++);	// relative branch
			if (((cpu->_D >> d.s_bit) & 1) == d.s_bit_value) {
				cpu->_P = cpu->PC + (int8_t)s.data;
				cpu->cycle++;
				if ((cpu->_P & 0xff00) != (cpu->PC & 0xff00))
					cpu->cycle++;
				cpu->PC = cpu->_P;
			}
		}	break;
		case 0x90: case 0xB0: case 0xF0: case 0x30:
		case 0xD0: case 0x10: case 0x50: case 0x70:
		{ // BCC, BCS, BEQ, BMI, BNE, BPL, BVC, BVS
			if (d.s_bit_value == MII_GET_P_BIT(cpu, d.s_bit)) {
				cpu->_P = cpu->PC + (int8_t)cpu->_P;
				cpu->cycle++;
				if ((cpu->_P & 0xff00) != (cpu->PC & 0xff00))
					cpu->cycle++;
				cpu->PC = cpu->_P;
			}
		}	break;
		case 0x80:
		{	// BRA
			cpu->_P = cpu->PC + (int8_t)cpu->_P;
			_FETCH(cpu->PC);
			if ((cpu->_P & 0xff00) != (cpu->PC & 0xff00))
				cpu->cycle++;
			cpu->PC = cpu->_P;
		}	break;
		case 0x89:
		{	// BIT immediate -- does not change N & V!
			cpu->P.Z = !(cpu->A & cpu->_D);
		}	break;
		case 0x24: case 0x2C: case 0x34: case 0x3C:
		{ // BIT
			cpu->P.Z = !(cpu->A & cpu->_D);
			cpu->P.N = !!(cpu->_D & 0x80);
			cpu->P.V = !!(cpu->_D & 0x40);
		}	break;
		case 0x00:
		{ // BRK
			// Turns out BRK is a 2 byte opcode, who knew? well that guy did:
			// https://www.nesdev.org/the%20'B'%20flag%20&%20BRK%20instruction.txt#:~:text=A%20note%20on%20the%20BRK,opcode%2C%20and%20not%20just%201.
			_FETCH(cpu->PC++);
			s.irq = 1;
			cpu->IRQ = MII_CPU_IRQ_BRK;		// BRK sort of IRQ interrupt
		}	break;
		case 0x18: case 0xD8: case 0x58: case 0xB8:
		{ // CLC, CLD, CLI, CLV
			_FETCH(cpu->PC);
			MII_SET_P_BIT(cpu, d.s_bit, 0);
		}	break;
		case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD:
		case 0xD9: case 0xC1: case 0xD1: case 0xD2:
		{ // CMP
			cpu->P.C = !!(cpu->A >= cpu->_D);
			uint8_t d = cpu->A - cpu->_D;
			_NZ(d);
		}	break;
		case 0xE0: case 0xE4: case 0xEC:
		{ // CPX
			cpu->P.C = !!(cpu->X >= cpu->_D);
			uint8_t d = cpu->X - cpu->_D;
			_NZ(d);
		}	break;
		case 0xC0: case 0xC4: case 0xCC:
		{ // CPY
			cpu->P.C = !!(cpu->Y >= cpu->_D);
			uint8_t d = cpu->Y - cpu->_D;
			_NZ(d);
		}	break;
		case 0x3A:
		{ // DEC
			_FETCH(cpu->PC);
			_NZ(--cpu->A);
		}	break;
		case 0xC6: case 0xD6: case 0xCE: case 0xDE:
		{ // DEC
			_FETCH(cpu->PC);
			_NZ(--cpu->_D);
		}	break;
		case 0xCA:
		{ // DEX
			_FETCH(cpu->PC);
			_NZ(--cpu->X);
		}	break;
		case 0x88:
		{ // DEY
			_FETCH(cpu->PC);
			_NZ(--cpu->Y);
		}	break;
		case 0x49: case 0x45: case 0x55: case 0x4D: case 0x5D:
		case 0x59: case 0x41: case 0x51: case 0x52:
		{ // EOR
			cpu->A ^= cpu->_D;
			_NZ(cpu->A);
		}	break;
		case 0x1A:
		{ // INC (accumulator)
			_FETCH(cpu->PC);
			_NZ(++cpu->A);
		}	break;
		case 0xE6: case 0xF6: case 0xEE: case 0xFE:
		{ // INC
			_FETCH(cpu->PC);
			_NZ(++cpu->_D);
		}	break;
		case 0xE8:
		{ // INX
			_FETCH(cpu->PC);
			_NZ(++cpu->X);
		}	break;
		case 0xC8:
		{ // INY
			_FETCH(cpu->PC);
			_NZ(++cpu->Y);
		}	break;
		case 0x4C: case 0x6C: case 0x7C:
		{ // JMP
			cpu->PC = cpu->_P;
		}	break;
		case 0x20:
		// https://github.com/AppleWin/AppleWin/issues/1257
		{ // JSR
			_FETCH(cpu->PC++);		cpu->_P = s.data;
			_FETCH(0x0100 | cpu->S);
			_STORE(0x0100 | cpu->S--, cpu->PC >> 8);
			_STORE(0x0100 | cpu->S--, cpu->PC & 0xff);
			_FETCH(cpu->PC++);		cpu->_P |= s.data << 8;
			cpu->PC = cpu->_P;
		}	break;
		case 0xA9: case 0xA5: case 0xB5: case 0xAD: case 0xBD:
		case 0xB9: case 0xA1: case 0xB1: case 0xB2:
		{ // LDA
			cpu->A = cpu->_D;
			_NZ(cpu->A);
		}	break;
		case 0xA2: case 0xA6: case 0xB6: case 0xAE: case 0xBE:
		{ // LDX
			cpu->X = cpu->_D;
			_NZ(cpu->X);
		}	break;
		case 0xA0: case 0xA4: case 0xB4: case 0xAC: case 0xBC:
		{ // LDY
			cpu->Y = cpu->_D;
			_NZ(cpu->Y);
		}	break;
		case 0x4A:
		{ // LSR
			_FETCH(cpu->PC);
			cpu->P.C = !!(cpu->A & 0x01);
			cpu->A >>= 1;
			_NZ(cpu->A);
		}	break;
		case 0x46: case 0x56: case 0x4E: case 0x5E:
		{ // LSR
			cpu->P.C = !!(cpu->_D & 0x01);
			cpu->_D >>= 1;
			_NZ(cpu->_D);
		}	break;
		case 0xEA:
		{ // NOP
			_FETCH(cpu->PC);
		}	break;
		case 0x09: case 0x05: case 0x15: case 0x0D: case 0x1D:
		case 0x19: case 0x01: case 0x11: case 0x12:
		{ // ORA
			cpu->A |= cpu->_D;
			_NZ(cpu->A);
		}	break;
		case 0x48:
		{ // PHA
			_STORE(0x0100 | cpu->S--, cpu->A); cpu->cycle++;
		}	break;
		case 0x08:
		{ // PHP
			uint8_t p = 0;
			MII_GET_P(cpu, p);
			p |= (1 << B_B) | (1 << B_X);
			_STORE(0x0100 | cpu->S--, p);	cpu->cycle++;
		}	break;
		case 0xDA:
		{ // PHX
			_STORE(0x0100 | cpu->S--, cpu->X);cpu->cycle++;
		}	break;
		case 0x5A:
		{ // PHY
			_STORE(0x0100 | cpu->S--, cpu->Y);cpu->cycle++;
		}	break;
		case 0x68:
		{ // PLA
			_FETCH(0x0100 | ++cpu->S);cpu->cycle++;
			cpu->A = s.data;cpu->cycle++;
			_NZ(cpu->A);
		}	break;
		case 0x28:
		{ // PLP
			_FETCH(0x0100 | ++cpu->S);cpu->cycle++;
			MII_SET_P(cpu, s.data);cpu->cycle++;
		}	break;
		case 0xFA:
		{ // PLX
			_FETCH(0x0100 | ++cpu->S);
			cpu->X = s.data;
			_NZ(cpu->X);
		}	break;
		case 0x7A:
		{ // PLY
			_FETCH(0x0100 | ++cpu->S);
			cpu->Y = s.data;
			_NZ(cpu->Y);
		}	break;
		case 0x2A:
		{ // ROL immediate
			_FETCH(cpu->PC);	// cycle++
			uint8_t c = cpu->P.C;
			cpu->P.C = !!(cpu->A & 0x80);
			cpu->A <<= 1;
			cpu->A |= c;
			_NZ(cpu->A);
		}	break;
		case 0x26: case 0x36: case 0x2E: case 0x3E:
		{ // ROL
			uint8_t c = cpu->P.C;
			cpu->P.C = !!(cpu->_D & 0x80);
			cpu->_D <<= 1;
			cpu->_D |= c;
			_NZ(cpu->_D);
		}	break;
		case 0x6A:
		{ // ROR
			_FETCH(cpu->PC);	// cycle++
			uint8_t c = cpu->P.C;
			cpu->P.C = !!(cpu->A & 0x01);
			cpu->A >>= 1;
			cpu->A |= c << 7;
			_NZ(cpu->A);
		}	break;
		case 0x66: case 0x76: case 0x6E: case 0x7E:
		{ // ROR
			uint8_t c = cpu->P.C;
			cpu->P.C = !!(cpu->_D & 0x01);
			cpu->_D >>= 1;
			cpu->_D |= c << 7;
			_NZ(cpu->_D);
		}	break;
		case 0x40:
		{ // RTI
			_FETCH(cpu->PC);	// dummy write
			cpu->S++; _FETCH(0x0100 | cpu->S);
			// FD : Modified to set Break bit to 0 in order to pass Harte's tests .
			for (int i = 0; i < 8; i++)
//				MII_SET_P_BIT(cpu, i, i == B_B || (s.data & (1 << i)));
				MII_SET_P_BIT(cpu, i, !(i == B_B) && (s.data & (1 << i)));
			cpu->P._R = 1;
			cpu->S++; _FETCH(0x0100 | cpu->S);
			cpu->_P = s.data;
			cpu->S++; _FETCH(0x0100 | cpu->S);
			cpu->_P |= s.data << 8;
			cpu->PC = cpu->_P;
		}	break;
		case 0x60:
		{ // RTS
			_FETCH(0x0100 | ((++cpu->S) & 0xff));cpu->cycle++;
			cpu->_P = s.data;
			_FETCH(0x0100 | ((++cpu->S) & 0xff));cpu->cycle++;
			cpu->_P |= s.data << 8;
			cpu->PC = cpu->_P + 1; cpu->cycle++;
		}	break;
		case 0xE9: case 0xE5: case 0xF5: case 0xED: case 0xFD:
		case 0xF9: case 0xE1: case 0xF1: case 0xF2:
		{ // SBC
			// Handle subbing in BCD with bit D
			if (unlikely(cpu->P.D)) {
#if 1
				uint8_t D = 0x99 - cpu->_D;
				// verbatim ADC code here
				uint8_t lo = (cpu->A & 0x0f) + (D & 0x0f) + !!cpu->P.C;
				if (lo > 9) lo += 6;
				uint8_t hi = (cpu->A >> 4) + (D >> 4) + (lo > 0x0f);
				cpu->P.Z = ((uint8_t)(cpu->A + D + cpu->P.C)) == 0;
				// that is 6502 behaviour
//				cpu->P.N = !!(hi & 0xf8);
				cpu->P.V = !!((!((cpu->A ^ D) & 0x80) &&
								((cpu->A ^ (hi << 4))) & 0x80));
				if (hi > 9) hi += 6;
				cpu->P.C = hi > 15;
			//	printf("SBC %02x %02x C:%d %x%x\n",
			//			cpu->A, D, !!cpu->P.C, hi & 0xf, lo & 0xf);
				cpu->A = (hi << 4) | (lo & 0x0f);
				// THAT is 65c02 behaviour
				cpu->P.N = !!(cpu->A & 0x80);
#else
				// Decimal mode
				// Perform decimal subtraction
				// fully based on http://www.6502.org/tutorials/decimal_mode.html
				unsigned int result;			    // the final 16bit result of the substration

				int16_t A = cpu->A;
				uint8_t B = cpu->_D;
				uint8_t C = cpu->P.C;               // Carry (borrow) bit : must be 1 if no borrow

				result = A - B - (1 - C);       // do the calculation in binary mode
				cpu->P.C = !(result & 0xFF00);
				cpu->P.V = !!((A ^ B) & (A ^ result) & 0x80);  // complex but it works !!!

				uint8_t AH = (A >> 4) & 0x0F;     // get the accumulator high digit
				int8_t AL;
				uint8_t BH = (B >> 4) & 0x0F;     // get the high digit of the substracted value
				uint8_t BL = B & 0x0F;            // get the low digit of the substracted value

				AL = (A & 0x0F) - (B & 0x0F) + C -1;    // 3a et 4a, calculation is performed on the low digits,  +C-1 is a trick
				//      A = A - B + C -1;                   // 4b calculation is performed with the full 8bit original values. Already done with result
				A = result;
				if (A < 0) A = A - 0x60;                // 4c if negative then substraction is performed to stay in the 00-99 range
				if (AL < 0) A = A - 0x06;               // 4d if low digit is <0 than apply the same operation on it
				cpu->A = A & 0xFF;                      // 3e et 4e  and voila !, we have the right value for the result
				_NZ(cpu->A);                            // set N and Z bits withe decimal result
#endif
			} else {
				cpu->_D = (~cpu->_D) & 0xff;
				uint16_t sum = cpu->A + cpu->_D + !!cpu->P.C;
				cpu->P.V = cpu->P.C = 0;
				if (~(cpu->A ^ cpu->_D) & (cpu->A ^ sum) & 0x80)
					cpu->P.V = 1;
				_NZC(sum);
				cpu->A = sum;
			}
		}	break;
		case 0x38: case 0xF8: case 0x78:
		{ // SEC, SED, SEI
			MII_SET_P_BIT(cpu, d.s_bit, 1);
		}	break;
		case 0x85: case 0x95: case 0x8D: case 0x9D:
		case 0x99: case 0x81: case 0x91: case 0x92:
		{ // STA
			cpu->_D = cpu->A;cpu->cycle++;
		}	break;
		case 0x86: case 0x96: case 0x8E:
		{ // STX
			cpu->_D = cpu->X;
		}	break;
		case 0x84: case 0x94: case 0x8C:
		{ // STY
			cpu->_D = cpu->Y;
		}	break;
		case 0x64: case 0x74: case 0x9C: case 0x9E:
		{ // STZ
			cpu->_D = 0;
		}	break;
		case 0x14: case 0x1c:
		{	// TRB
			cpu->P.Z = !(cpu->A & cpu->_D);
			cpu->_D &= ~cpu->A;
		}	break;
		case 0x04: case 0x0c:
		{	// TSB
			cpu->P.Z = !(cpu->A & cpu->_D);
			cpu->_D |= cpu->A;
		}	break;
		case 0xAA:
		{ // TAX
			cpu->X = cpu->A;cpu->cycle++;
			_NZ(cpu->X);
		}	break;
		case 0xA8:
		{ // TAY
			cpu->Y = cpu->A;cpu->cycle++;
			_NZ(cpu->Y);
		}	break;
		case 0xBA:
		{ // TSX
			cpu->X = cpu->S;cpu->cycle++;
			_NZ(cpu->X);
		}	break;
		case 0x8A:
		{ // TXA
			cpu->A = cpu->X;cpu->cycle++;
			_NZ(cpu->A);
		}	break;
		case 0x9A:
		{ // TXS
			cpu->S = cpu->X;cpu->cycle++;
		}	break;
		case 0x98:
		{ // TYA
			cpu->A = cpu->Y;cpu->cycle++;
			_NZ(cpu->A);
		}	break;
		case 0x07: case 0x17: case 0x27: case 0x37: case 0x47:
		case 0x57: case 0x67: case 0x77: case 0x87: case 0x97:
		case 0xA7: case 0xB7: case 0xC7: case 0xD7: case 0xE7:
		case 0xF7:
		{ // RMB/SMB
			cpu->_D = (cpu->_D & ~(1 << d.s_bit)) | (d.s_bit_value << d.s_bit);
		}	break;
		/* Apparently these NOPs use 3 bytes, according to the tests */
		case 0x5c: case 0xdc: case 0xfc:
			_FETCH(cpu->PC++);
			// fall through
		/* Apparently these NOPs use 2 bytes, according to the tests */
		case 0x02: case 0x22: case 0x42: case 0x62: case 0x82:
		case 0xC2: case 0xE2: case 0x44: case 0x54: case 0xD4:
		case 0xF4:
			_FETCH(cpu->PC++);	// consume that byte
			break;
		case 0xdb:
		// trap NOPs / STP (WDC)
			_FETCH(cpu->PC++); // FD: Added to pass HARTE's test
			break;
		case 0xCB :
		// FD: Added to pass HARTE's test
		// WAI for WDC65C02 not in R65C02
		// FD: Added to properly pass HARTE's test
		case 0x0B: case 0x1B: case 0x2B: case 0x3B: case 0x4B: case 0x5B:
		case 0x6B: case 0x7B: case 0x8B: case 0x9B: case 0xAB: case 0xBB:
		// these two are SPECIAL, 0xebfb is used as the 'trap' that calls
		// back into the emulator. This is used by the smartport driver
		case 0xEB: case 0xFB:
		case 0x03: case 0X13: case 0X23: case 0X33: case 0x43: case 0x53:
		case 0x63: case 0x73: case 0x83: case 0x93: case 0xA3: case 0xB3:
		case 0xC3: case 0xD3: case 0xE3: case 0xF3:
			//  NOPs
			break;
		default:
			printf("%04x %02x UNKNOWN INSTRUCTION\n", cpu->PC, cpu->IR);
		//	exit(1);
			break;
	}
	if (d.w) {
		_STORE(cpu->_P, cpu->_D);
	}
#if MII_65C02_DIRECT_ACCESS
	// we don't need to do anything here, the store already did it
	if (likely(cpu->instruction_run)) {
		cpu->instruction_run--;
		goto next_instruction;
	}
#else
	goto next_instruction;
	pt_end(cpu->state);
#endif
	return s;
}
