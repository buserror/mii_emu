/*
 * mui_65c02_ops.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

typedef struct mii_op_desc_t {
	uint32_t 		op : 8,
					mode : 4,
					pc : 2,
					branch : 1,	// relative branch
					ch_pc : 1,	// change PC
					s_bit : 3,
					s_bit_value : 1,
					r : 1,
					w : 1;
} mii_op_desc_t;

typedef struct mii_op_t {
	char 			name[4];
	mii_op_desc_t 	desc;
} mii_op_t;

enum {
	B_C = 0,
	B_Z,
	B_I,
	B_D,
	B_B,
	B_X,
	B_V,
	B_N,
};

enum {
	IMPLIED,  // BRK
	IMM,      // LDA #$01
	ZP_REL,   // LDA $C0 or BCC $FF
	ZP_X,     // LDA $C0,X
	ZP_Y,     // LDX $C0,Y
	ABS,      // LDA $1234
	ABS_X,    // LDA $1234,X
	ABS_Y,    // LDA $1234,Y
	IND_X,    // LDA ($FF,X)
	IND_AX,   // JMP ($1234,X)
	IND_Y,    // LDA ($FF),Y
	IND,      // JMP ($1234)
	IND_Z,	// LDA ($C0)
	BRANCH,	// BEQ/BNE etc
};

#define PCODE_(_name, _mode, _op, _pc, _r, _w, _br, _sb, _sv, _cpc) \
	[_op] = { \
		.name = #_name, \
		.desc = { \
			.op = _op, .mode = _mode, .pc = _pc, .r = _r, .w = _w, \
			.branch = _br, .s_bit = _sb, .s_bit_value = _sv, .ch_pc = _cpc } \
	},

#ifndef MII_CPU_65C02_IMPL
extern const mii_op_t mii_cpu_op[256];

#else // MII_CPU_65C02_IMPL

#define PCODE_R_(_name, _mode, _op, _pc) \
	PCODE_(_name, _mode, _op, _pc, 1, 0, 0, 0, 0, 0)
#define PCODE__W(_name, _mode, _op, _pc) \
	PCODE_(_name, _mode, _op, _pc, 0, 1, 0, 0, 0, 0)

#define PCODE___(_name, _mode, _op, _pc) \
	PCODE_(_name, _mode, _op, _pc, 0, 0, 0, 0, 0, 0)

#define PCODE_RW(_name, _mode, _op, _pc) \
	PCODE_(_name, _mode, _op, _pc, 1, 1, 0, 0, 0, 0)
#define PCODE__W(_name, _mode, _op, _pc) \
	PCODE_(_name, _mode, _op, _pc, 0, 1, 0, 0, 0, 0)

#define PCODE_BR(_name, _op, _pc, _sb, _sv) \
	PCODE_(_name, BRANCH, _op, _pc, 0, 0, 1, B_##_sb, _sv, 1)
#define PCODE_PC(_name, _mode, _op, _pc) \
	PCODE_(_name, _mode, _op, _pc, 1, 0, 0, 0, 0, 1)
#define PCODE_RT(_name, _mode, _op, _pc) \
	PCODE_(_name, _mode, _op, _pc, 0, 0, 0, 0, 0, 1)

#define PCODE_SB(_name, _mode, _op, _pc, _sb, _sv) \
	PCODE_(_name, _mode, _op, _pc, 0, 0, 0, B_##_sb, _sv, 0)
// for RMBx SMBx
#define PCODE_MB(_name, _op, _sb, _sv) \
	PCODE_(_name, ZP_REL, _op, 2, 1, 1, 0, _sb, _sv, 0)
// for BBRx BBSx
#define PCODE_BB(_name, _op, _sb, _sv) \
	PCODE_(_name, ZP_REL, _op, 3, 1, 0, 1, _sb, _sv, 0)

const mii_op_t mii_cpu_op[256] = {
	PCODE___(ADC, IMM, 		0x69, 2)
	PCODE_R_(ADC, ZP_REL, 	0x65, 2)
	PCODE_R_(ADC, ZP_X, 	0x75, 2)
	PCODE_R_(ADC, ABS, 		0x6D, 3)
	PCODE_R_(ADC, ABS_X, 	0x7D, 3)
	PCODE_R_(ADC, ABS_Y, 	0x79, 3)
	PCODE_R_(ADC, IND_X, 	0x61, 2)
	PCODE_R_(ADC, IND_Y, 	0x71, 2)
	PCODE_R_(ADC, IND_Z, 	0x72, 2)
	PCODE___(AND, IMM, 		0x29, 2)
	PCODE_R_(AND, ZP_REL, 	0x25, 2)
	PCODE_R_(AND, ZP_X, 	0x35, 2)
	PCODE_R_(AND, ABS, 		0x2D, 3)
	PCODE_R_(AND, ABS_X,	0x3D, 3)
	PCODE_R_(AND, ABS_Y, 	0x39, 3)
	PCODE_R_(AND, IND_X, 	0x21, 2)
	PCODE_R_(AND, IND_Y, 	0x31, 2)
	PCODE_R_(AND, IND_Z,	0x32, 2)
	PCODE___(ASL, IMPLIED, 	0x0A, 1)
	PCODE_RW(ASL, ZP_REL, 	0x06, 2)
	PCODE_RW(ASL, ZP_X,		0x16, 2)
	PCODE_RW(ASL, ABS, 		0x0E, 3)
	PCODE_RW(ASL, ABS_X, 	0x1E, 3)
	PCODE_BB(BBR0, 			0x0F, 0, 0)
	PCODE_BB(BBR1, 			0x1F, 1, 0)
	PCODE_BB(BBR2, 			0x2F, 2, 0)
	PCODE_BB(BBR3, 			0x3F, 3, 0)
	PCODE_BB(BBR4, 			0x4F, 4, 0)
	PCODE_BB(BBR5, 			0x5F, 5, 0)
	PCODE_BB(BBR6, 			0x6F, 6, 0)
	PCODE_BB(BBR7, 			0x7F, 7, 0)
	PCODE_BB(BBS0, 			0x8F, 0, 1)
	PCODE_BB(BBS1, 			0x9F, 1, 1)
	PCODE_BB(BBS2, 			0xAF, 2, 1)
	PCODE_BB(BBS3, 			0xBF, 3, 1)
	PCODE_BB(BBS4, 			0xCF, 4, 1)
	PCODE_BB(BBS5, 			0xDF, 5, 1)
	PCODE_BB(BBS6, 			0xEF, 6, 1)
	PCODE_BB(BBS7, 			0xFF, 7, 1)
	PCODE_BR(BCC, 			0x90, 2, C, 0)
	PCODE_BR(BCS, 			0xB0, 2, C, 1)
	PCODE_BR(BEQ, 			0xF0, 2, Z, 1)
	PCODE_BR(BMI, 			0x30, 2, N, 1)
	PCODE_BR(BNE, 			0xD0, 2, Z, 0)
	PCODE_BR(BPL, 			0x10, 2, N, 0)
	PCODE_BR(BVC, 			0x50, 2, V, 0)
	PCODE_BR(BVS, 			0x70, 2, V, 1)
	PCODE_BR(BRA, 			0x80, 2, X, 1)
	PCODE_R_(BIT, ZP_REL,	0x24, 2)
	PCODE_R_(BIT, ABS,		0x2C, 3)
	PCODE___(BIT, IMM,		0x89, 2)
	PCODE_R_(BIT, ZP_X,		0x34, 2)
	PCODE_R_(BIT, ABS_X,	0x3C, 3)
	PCODE_RT(BRK, IMPLIED,	0x00, 1)
	PCODE_SB(CLC, IMPLIED,	0x18, 1, C, 0)
	PCODE_SB(CLD, IMPLIED,	0xD8, 1, D, 0)
	PCODE_SB(CLI, IMPLIED,	0x58, 1, I, 0)
	PCODE_SB(CLV, IMPLIED,	0xB8, 1, V, 0)
	PCODE___(CMP, IMM, 		0xC9, 2)
	PCODE_R_(CMP, ZP_REL,	0xC5, 2)
	PCODE_R_(CMP, ZP_X,		0xD5, 2)
	PCODE_R_(CMP, ABS_X,	0xDD, 3)
	PCODE_R_(CMP, ABS,		0xCD, 3)
	PCODE_R_(CMP, ABS_Y,	0xD9, 3)
	PCODE_R_(CMP, IND_X,	0xC1, 2)
	PCODE_R_(CMP, IND_Y,	0xD1, 2)
	PCODE_R_(CMP, IND_Z,	0xD2, 2)
	PCODE___(CPX, IMM, 		0xE0, 2)
	PCODE_R_(CPX, ZP_REL,	0xE4, 2)
	PCODE_R_(CPX, ABS,		0xEC, 3)
	PCODE___(CPY, IMM, 		0xC0, 2)
	PCODE_R_(CPY, ZP_REL, 	0xC4, 2)
	PCODE_R_(CPY, ABS,		0xCC, 3)
	PCODE___(DEC, IMPLIED,	0x3A, 1)
	PCODE_RW(DEC, ZP_REL,	0xC6, 2)
	PCODE_RW(DEC, ZP_X,		0xD6, 2)
	PCODE_RW(DEC, ABS,		0xCE, 3)
	PCODE_RW(DEC, ABS_X,	0xDE, 3)
	PCODE___(DEX, IMPLIED,	0xCA, 1)
	PCODE___(DEY, IMPLIED,	0x88, 1)
	PCODE___(EOR, IMM,		0x49, 2)
	PCODE_R_(EOR, ZP_REL,	0x45, 2)
	PCODE_R_(EOR, ZP_X,		0x55, 2)
	PCODE_R_(EOR, ABS,		0x4D, 3)
	PCODE_R_(EOR, ABS_X,	0x5D, 3)
	PCODE_R_(EOR, ABS_Y,	0x59, 3)
	PCODE_R_(EOR, IND_X,	0x41, 2)
	PCODE_R_(EOR, IND_Y,	0x51, 2)
	PCODE_R_(EOR, IND_Z,	0x52, 2)
	PCODE___(INC, IMPLIED,	0x1A, 1)
	PCODE_RW(INC, ZP_REL,	0xE6, 2)
	PCODE_RW(INC, ZP_X,		0xF6, 2)
	PCODE_RW(INC, ABS,		0xEE, 3)
	PCODE_RW(INC, ABS_X,	0xFE, 3)
	PCODE___(INX, IMPLIED,	0xE8, 1)
	PCODE___(INY, IMPLIED,	0xC8, 1)
	PCODE_PC(JMP, ABS,		0x4C, 3)
	PCODE_PC(JMP, IND,		0x6C, 3)
	PCODE_PC(JMP, IND_AX,	0x7C, 3)
	PCODE___(JSR, IMPLIED,	0x20, 3)
	PCODE___(LDA, IMM,		0xA9, 2)
	PCODE_R_(LDA, ZP_REL,	0xA5, 2)
	PCODE_R_(LDA, ZP_X,		0xB5, 2)
	PCODE_R_(LDA, ABS,		0xAD, 3)
	PCODE_R_(LDA, ABS_X,	0xBD, 3)
	PCODE_R_(LDA, ABS_Y,	0xB9, 3)
	PCODE_R_(LDA, IND_X,	0xA1, 2)
	PCODE_R_(LDA, IND_Y,	0xB1, 2)
	PCODE_R_(LDA, IND_Z,	0xB2, 2)
	PCODE___(LDX, IMM,		0xA2, 2)
	PCODE_R_(LDX, ZP_REL,	0xA6, 2)
	PCODE_R_(LDX, ZP_Y,		0xB6, 2)
	PCODE_R_(LDX, ABS,		0xAE, 3)
	PCODE_R_(LDX, ABS_Y,	0xBE, 3)
	PCODE___(LDY, IMM,		0xA0, 2)
	PCODE_R_(LDY, ZP_REL,	0xA4, 2)
	PCODE_R_(LDY, ZP_X,		0xB4, 2)
	PCODE_R_(LDY, ABS,		0xAC, 3)
	PCODE_R_(LDY, ABS_X,	0xBC, 3)
	PCODE___(LSR, IMPLIED,	0x4A, 1)
	PCODE_RW(LSR, ZP_REL,	0x46, 2)
	PCODE_RW(LSR, ZP_X,		0x56, 2)
	PCODE_RW(LSR, ABS,		0x4E, 3)
	PCODE_RW(LSR, ABS_X,	0x5E, 3)
	PCODE___(NOP, IMPLIED,	0xEA, 1)
	PCODE___(ORA, IMM,		0x09, 2)
	PCODE_R_(ORA, ZP_REL,	0x05, 2)
	PCODE_R_(ORA, ZP_X,		0x15, 2)
	PCODE_R_(ORA, ABS,		0x0D, 3)
	PCODE_R_(ORA, ABS_X,	0x1D, 3)
	PCODE_R_(ORA, ABS_Y,	0x19, 3)
	PCODE_R_(ORA, IND_X,	0x01, 2)
	PCODE_R_(ORA, IND_Y,	0x11, 2)
	PCODE_R_(ORA, IND_Z,	0x12, 2)
	PCODE___(PHA, IMPLIED,	0x48, 1)
	PCODE___(PHP, IMPLIED,	0x08, 1)
	PCODE___(PHX, IMPLIED,	0xDA, 1)
	PCODE___(PHY, IMPLIED,	0x5A, 1)
	PCODE___(PLA, IMPLIED,	0x68, 1)
	PCODE___(PLP, IMPLIED,	0x28, 1)
	PCODE___(PLX, IMPLIED,	0xFA, 1)
	PCODE___(PLY, IMPLIED,	0x7A, 1)
	// these fill the opcode name without terminating zero
	PCODE_MB(RMB0, 			0x07, 0, 0)
	PCODE_MB(RMB1, 			0x17, 1, 0)
	PCODE_MB(RMB2, 			0x27, 2, 0)
	PCODE_MB(RMB3, 			0x37, 3, 0)
	PCODE_MB(RMB4, 			0x47, 4, 0)
	PCODE_MB(RMB5, 			0x57, 5, 0)
	PCODE_MB(RMB6, 			0x67, 6, 0)
	PCODE_MB(RMB7, 			0x77, 7, 0)
	PCODE___(ROL, IMPLIED,	0x2A, 1)
	PCODE_RW(ROL, ZP_REL, 	0x26, 2)
	PCODE_RW(ROL, ZP_X, 	0x36, 2)
	PCODE_RW(ROL, ABS, 		0x2E, 3)
	PCODE_RW(ROL, ABS_X, 	0x3E, 3)
	PCODE___(ROR, IMPLIED,	0x6A, 1)
	PCODE_RW(ROR, ZP_REL,	0x66, 2)
	PCODE_RW(ROR, ZP_X,		0x76, 2)
	PCODE_RW(ROR, ABS,		0x6E, 3)
	PCODE_RW(ROR, ABS_X,	0x7E, 3)
	PCODE_RT(RTI, IMPLIED,	0x40, 1)
	PCODE_RT(RTS, IMPLIED,	0x60, 1)
	PCODE___(SBC, IMM,		0xE9, 2)
	PCODE_R_(SBC, ZP_REL,	0xE5, 2)
	PCODE_R_(SBC, ZP_X,		0xF5, 2)
	PCODE_R_(SBC, ABS,		0xED, 3)
	PCODE_R_(SBC, ABS_X,	0xFD, 3)
	PCODE_R_(SBC, ABS_Y,	0xF9, 3)
	PCODE_R_(SBC, IND_X,	0xE1, 2)
	PCODE_R_(SBC, IND_Y,	0xF1, 2)
	PCODE_R_(SBC, IND_Z,	0xF2, 2)
	PCODE_SB(SEC, IMPLIED,	0x38, 1, C, 1)
	PCODE_SB(SED, IMPLIED,	0xF8, 1, D, 1)
	PCODE_SB(SEI, IMPLIED,	0x78, 1, I, 1)
	// these fill the opcode name without terminating zero
	PCODE_MB(SMB0, 			0x87, 0, 1)
	PCODE_MB(SMB1, 			0x97, 1, 1)
	PCODE_MB(SMB2, 			0xa7, 2, 1)
	PCODE_MB(SMB3, 			0xb7, 3, 1)
	PCODE_MB(SMB4, 			0xc7, 4, 1)
	PCODE_MB(SMB5, 			0xd7, 5, 1)
	PCODE_MB(SMB6, 			0xe7, 6, 1)
	PCODE_MB(SMB7, 			0xf7, 7, 1)
	PCODE__W(STA, ZP_REL,	0x85, 2)
	PCODE__W(STA, ZP_X,		0x95, 2)
	PCODE__W(STA, ABS, 		0x8D, 3)
	PCODE__W(STA, ABS_X,	0x9D, 3)
	PCODE__W(STA, ABS_Y,	0x99, 3)
	PCODE__W(STA, IND_X,	0x81, 2)
	PCODE__W(STA, IND_Y,	0x91, 2)
	PCODE__W(STA, IND_Z,	0x92, 2)
	PCODE__W(STX, ZP_REL,	0x86, 2)
	PCODE__W(STX, ZP_Y,		0x96, 2)
	PCODE__W(STX, ABS,		0x8E, 3)
	PCODE__W(STY, ZP_REL,	0x84, 2)
	PCODE__W(STY, ZP_X,		0x94, 2)
	PCODE__W(STY, ABS,		0x8C, 3)
	PCODE__W(STZ, ZP_REL,	0x64, 2)
	PCODE__W(STZ, ZP_X,		0x74, 2)
	PCODE__W(STZ, ABS,		0x9C, 3)
	PCODE__W(STZ, ABS_X,	0x9E, 3)
	PCODE___(TAX, IMPLIED,	0xAA, 1)
	PCODE___(TAY, IMPLIED,	0xA8, 1)
	PCODE_RW(TRB, ZP_REL,	0x14, 2)
	PCODE_RW(TRB, ABS,		0x1c, 3)
	PCODE___(TSX, IMPLIED,	0xBA, 1)
	PCODE_RW(TSB, ZP_REL,	0x04, 2)
	PCODE_RW(TSB, ABS,		0x0c, 3)
	PCODE___(TXA, IMPLIED,	0x8A, 1)
	PCODE___(TXS, IMPLIED,	0x9A, 1)
	PCODE___(TYA, IMPLIED,	0x98, 1)
};


#endif // MII_CPU_65C02_IMPL
