/*
 * mii_65c02_asm.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mii_65c02.h"
#include "mii_65c02_ops.h"
#include "mii_65c02_asm.h"

static char *
_mii_extract_token(
	char *position,
	char *dst,
	int dst_len)
{
	if (!position)
		return NULL;
	while (*position == ' ' || *position == '\t')
		position++;
	char *kw = strsep(&position, " \t");
	if (kw)
		strncpy(dst, kw, dst_len-1);
	return position;
}


static char *
_mii_extract_name(
	char *src,
	char *dst,
	int dst_len)
{
	char *end = src;
	while (isalnum(*end) || *end == '_')
		*dst++ = *end++;
	*dst = 0;
	return end;
}

static const char * apple2_charset =
"@ABCDEFGHIJKLMNO"
"PQRSTUVWXYZ[\\]^_"
" !\"#$%&'()*+,-./"
"0123456789:;<=>?"
"................"
"................"
"`abcdefghijklmno"
"pqrstuvwxyz{|}~";

static char *
_mii_extract_value_or_name(
	mii_cpu_asm_line_t *l,
	char *src)
{
	static const char * const hex = "0123456789abcdef";
	char *end = src;
	l->op_value = 0;
	if (*end == '<') {
		l->op_low = 1;
		end++;
	} else if (*end == '>') {
		l->op_high = 1;
		end++;
	}
	if (*end == '$') {
		end++;
		while (isxdigit(*end))
			l->op_value = (l->op_value << 4) +
							(strchr(hex, tolower(*end++)) - hex);
	} else if (isdigit(*end)) {
		while (isdigit(*end))
			l->op_value = (l->op_value << 4) +
							(strchr(hex, tolower(*end++)) - hex);
	} else if (*end == '\'' || *end == '"') {
		end++;
		while (*end && *end != '\'' && *end != '"') {
			char *c = strchr(apple2_charset, *end++);
			if (c)
				l->op_value = (l->op_value << 8) + (c - apple2_charset);
		}
	} else {
		end = _mii_extract_name(end, l->op_name, sizeof(l->op_name));
		l->label_resolved = 0;
	}
	return end;
}

static int
_mii_resolve_symbol(
	mii_cpu_asm_program_t *p,
	mii_cpu_asm_line_t *l,
	int optional)
{
	if (!l->op_name[0] || l->label_resolved)
		return 1;
	mii_cpu_asm_line_t *l2 = p->sym;
	while (l2) {
		if (!strcasecmp(l->op_name, l2->label)) {
			l->op_value = l2->op_value;
			l->label_resolved = 1;
			goto found;
		}
		l2 = l2->sym_next;
	}
	if (!optional)
		printf("%s symbol %s not found\n", __func__, l->op_name);
	return 0;
found:
	if (l->op_low)
		l->op_value &= 0xff;
	else if (l->op_high)
		l->op_value >>= 8;
	return 1;
}

void
mii_cpu_asm_load(
	mii_cpu_asm_program_t *p,
	const char *prog)
{
	const char *current = prog;
	int line_count = 0;
	do {
		const char *lend = strchr(current, '\n');
		int len = lend ? (int)(lend - current) : (int)strlen(current);

		mii_cpu_asm_line_t *l = calloc(1, sizeof(mii_cpu_asm_line_t) + len + 1);
		l->line_index = line_count++;
		sprintf(l->line, "%.*s", len, current);
		if (p->prog_tail) {
			p->prog_tail->next = l;
			p->prog_tail = l;
		} else {
			p->prog = p->prog_tail = l;
		}
		char *dup = strdup(l->line);
		char * comment = strrchr(dup, ';');
		if (comment)
			*comment = 0;
		if (!*dup) {
			goto next_line;
		}
		char * position = dup;
		if (position[0] != ' ' && position[0] != '\t') {
			char *start = position;
			char *kw = strsep(&position, " \t=");
			if (kw) {
				// strip spaces
				char *ke = kw + strlen(kw);
				while (ke > start && (ke[-1] <= ' ' || ke[-1] == ':'))
					*--ke = 0;
				strncpy(l->label, kw, sizeof(l->label)-1);
			}
		}
		position = _mii_extract_token(position, l->mnemonic, sizeof(l->mnemonic));

		if (!strcmp(l->mnemonic, ".db") || !strcmp(l->mnemonic, "byte")) {
			/* remaining of the line is comma separated hex values OR symbols
			 * that can be resolved immediately */
			char *kw = position;
			while (*kw == ' ' || *kw == '\t') kw++;
			char *cur = NULL;
			while ((cur = strsep(&kw, ",")) != NULL) {
				while (*cur == ' ' || *cur == '\t') cur++;
				char *ke = cur + strlen(cur);
				while (ke > cur && (ke[-1] <= ' ' || ke[-1] == ':'))
					*--ke = 0;
				if (!*cur)
					break;
				_mii_extract_value_or_name(l, cur);
				if (_mii_resolve_symbol(p, l, 0) == 0)
					printf("ERROR -- sorry code symbols don't work, just EQUs\n");
				else
					l->opcodes[l->opcode_count++] = l->op_value;
			}
			goto next_line;
		}
		if (!strncmp(l->mnemonic, ".asc", 4) || !strcmp(l->mnemonic, "text")) {
			/* remaining of the line is a string, that we convert to apple2 charset
				strings are delimited with "" and can also have numerical values, so
			   a typical syntax is:
			   		.asc "Hello World", 0
			   */
			char *kw = position;
			do {
				while (*kw == ' ' || *kw == '\t') kw++;
				if (*kw == '"') {
					kw++;
					while (*kw && *kw != '"') {
						char *c = strchr(apple2_charset, *kw++);
						if (c)
							l->opcodes[l->opcode_count++] = 0x80 +
										(c - apple2_charset);
					}
				} else {
					kw = _mii_extract_value_or_name(l, kw);
					if (_mii_resolve_symbol(p, l, 0) == 0)
						printf("ERROR -- sorry code symbols don't work, just EQUs\n");
					else
						l->opcodes[l->opcode_count++] = l->op_value;
				}
				while (*kw == ' ' || *kw == '\t') kw++;
				if (*kw == ',')
					kw++;
				else
					break;
			} while (*kw);
			goto next_line;
		}
		position = _mii_extract_token(position, l->operand, sizeof(l->operand));
		if (l->operand[0] == '.' || l->operand[0] == '=') {
			// a directive that has been not aligned on the first line, re-adjust
			snprintf(l->label, sizeof(l->label), "%s", l->mnemonic);
			snprintf(l->mnemonic, sizeof(l->mnemonic), "%s", l->operand);
			position = _mii_extract_token(position, l->operand, sizeof(l->operand));
		}
		if (l->mnemonic[0] == '=' ||
				!strcasecmp(l->mnemonic, "equ") ||
				!strcasecmp(l->mnemonic, ".equ")) {
			l->symbol = 1;
			if (p->sym_tail)
				p->sym_tail->sym_next = l;
			else
				p->sym = l;
			_mii_extract_value_or_name(l, l->operand);
			if (_mii_resolve_symbol(p, l, 0) == 0)
				printf("ERROR -- sorry code symbols don't work, just EQUs\n");
		}
	//	printf(">L:%s M:%s O:%s\n", l->label, l->mnemonic, l->operand);
next_line:
		free(dup);
		current = lend ? lend+1 : NULL;
	} while (current);

}


static int
mii_cpu_opcode_has_mode(
	const char *mnemonic,
	int mode)
{
	for (int i = 0; i < 256; i++) {
		if (!strncasecmp(mnemonic, mii_cpu_op[i].name, 4)) {
			if (mode == mii_cpu_op[i].desc.mode) {
				return i;
			}
		}
	}
	return -1;
}

static int
_mii_cpu_asm_parse_operand(
	mii_cpu_asm_program_t *p,
	mii_cpu_asm_line_t *l)
{
	l->mode = IMPLIED;
	if (!l->operand[0])
		return 0;

	char sep = 0; // (parenthesis)
	char *src = l->operand;
	switch (*src) {
		case '#':
			l->mode = IMM;
			src = _mii_extract_value_or_name(l, src + 1);
			break;
		case '$':
			l->mode = ABS;
			src = _mii_extract_value_or_name(l, src);
			break;
		case '(':
			l->mode = IND;
			sep = ')';
			src = _mii_extract_value_or_name(l, src + 1);
			break;
		default: {
			l->mode = ABS;
			src = _mii_extract_value_or_name(l, src);
			_mii_resolve_symbol(p, l, 1);
		}	break;
	}
	while (*src == ' ' || *src == '\t') src++;
	switch (*src) {
		case ',':
			src++;
			while (*src == ' ' || *src == '\t') src++;
			switch (tolower(*src)) {
				case 'x':
					l->mode = ABS_X;
					if (sep) {
						// special case for JMP ($xxxx,x)
						if (!strcasecmp(l->mnemonic, "JMP"))
							l->mode = IND_AX;
						else
							l->mode = IND_X;
						break;
					}
					break;
				case 'y':
					l->mode = ABS_Y;
					break;
				default:
					l->mode = ABS;
					break;
			}
			break;
		case ')':
			src++;
			switch (*src) {
				case ',':
					src++;
					while (*src == ' ' || *src == '\t') src++;
					switch (tolower(*src)) {
						case 'x':
							l->mode = IND_X;
							break;
						default:
							l->mode = IND_Y;
							break;
					}
					break;
				default:
					l->mode = IND;
					break;
			}
			break;
	}
	switch (l->mode) {
		case ABS: {
			if (l->op_value < 0x100) {
				int newo = mii_cpu_opcode_has_mode(l->mnemonic, ZP_REL);
				if (newo != -1) {
					l->mode = ZP_REL;
					l->opcodes[0] = mii_cpu_op[newo].desc.op;
					l->opcode_count = mii_cpu_op[newo].desc.pc;
				}
			}
		}	break;
		case ABS_X: {
			if (l->op_value < 0x100) {
				int newo = mii_cpu_opcode_has_mode(l->mnemonic, ZP_X);
				if (newo != -1) {
					l->mode = ZP_X;
					l->opcodes[0] = mii_cpu_op[newo].desc.op;
					l->opcode_count = mii_cpu_op[newo].desc.pc;
				}
			}
		}	break;
		case ABS_Y: {
			if (l->op_value < 0x100) {
				int newo = mii_cpu_opcode_has_mode(l->mnemonic, ZP_Y);
				if (newo != -1) {
					l->mode = ZP_Y;
					l->opcodes[0] = mii_cpu_op[newo].desc.op;
					l->opcode_count = mii_cpu_op[newo].desc.pc;
				}
			}
		}	break;
		case IND: if (1) {
			if (l->op_value < 0x100) {
			//	printf("Testing if IND opcode %s can be IND_Z operand %s (%04x)\n",
			//		l->mnemonic, l->operand, l->op_value);
				int newo = mii_cpu_opcode_has_mode(l->mnemonic, IND_Z);
				if (newo != -1) {
			//		printf("YES replace %02x with %02x\n",
			//			l->opcodes[0], newo);
					l->mode = IND_Z;
					l->opcodes[0] = mii_cpu_op[newo].desc.op;
					l->opcode_count = mii_cpu_op[newo].desc.pc;
				}
			}
		}	break;
	}
	return 0;
}

int
mii_cpu_asm_assemble(
	mii_cpu_asm_program_t *p )
{
	mii_cpu_asm_line_t *l = NULL;
	int error = 0;

	// fix symbols
	l = p->sym;
	while (l) {
		_mii_extract_value_or_name(l, l->operand);
	//	printf("SYM %s = %04x\n", l->label, l->op_value);
		l = l->sym_next;
	}
	l = p->prog;
	while (l) {
		if (!l->mnemonic[0] || l->symbol) {
			l = l->next;
			continue;
		}
		l->mode = IMPLIED;
		_mii_cpu_asm_parse_operand(p, l);
		if (!strcasecmp(l->mnemonic, ".org")) {
			if (p->verbose)
				printf("%s origin set to $%04x\n", __func__, l->op_value);
			if (l->mode == ABS) {
				if (!p->org)
					p->org = l->op_value;
				l->addr_set = 1;
				l->addr = l->op_value;
			}
			l = l->next;
			continue;
		} else  if (!strcasecmp(l->mnemonic, ".verbose")) {
			p->verbose = 1;
			l = l->next;
			continue;
		} else if (l->mnemonic[0] == '.') {
			l = l->next;
			continue;
		}
		int found = -1;
		for (int i = 0; i < 256; i++) {
			if (!strncasecmp(l->mnemonic, mii_cpu_op[i].name, 4)) {
				if (mii_cpu_op[i].desc.branch) {
					l->mode = mii_cpu_op[i].desc.mode;
					found = i;
					break;
				} else if (l->mode == mii_cpu_op[i].desc.mode) {
					found = i;
					break;
				} else if (mii_cpu_op[i].desc.op == 0x20 &&
								l->mode == ABS) {
					// this is JSR -- TECHNICALLY it's ABS mode, but
					// it has to do a 'special' fetch so it's marked as
					// implied in the table
					found = i;
				}
			}
		}
		if (found == -1) {
			printf("ERROR: %d: %s %s %s\n",
				l->line_index, l->label, l->mnemonic, l->operand);
			printf("  Missing opcode for %s %d\n", l->mnemonic, l->mode);
			error = 1;
			break;
		}
	//	printf("FOUND %02x for '%s' '%s' '%s' name:'%s'\n",
	//		found, l->label, l->mnemonic, l->operand, l->op_name);
		l->opcodes[0] = mii_cpu_op[found].desc.op;
		l->opcode_count = mii_cpu_op[found].desc.pc;
		l = l->next;
	}
	if (error)
		return error;
	uint16_t pc = p->org;
	/*
	 * We (should) know all instruction size by now, so lets sets
	 * the addresses for all the lines
	 */
	l = p->prog;
	while (l) {
		if (l->addr_set)
			pc = l->addr;
		else
			l->addr = pc;
		if (l->mnemonic[0] != '.') {
			// this sets the ones we know about, and clears the ones we dont
			for (int i = 1; i < l->opcode_count; i++)
				l->opcodes[i] = l->op_value >> (8 * (i - 1));
		}
		pc += l->opcode_count;
		l = l->next;
	}
	/*
	 * Now resolve all the operand labels
	 */
	l = p->prog;
	while (l) {
		if (l->op_name[0] && !l->label_resolved) {
			mii_cpu_asm_line_t *l2 = p->prog;
			while (l2) {
				int32_t value = 0;
				if (!strcasecmp(l->op_name, l2->label)) {
				//	value = l2->op_value;
					if (!l2->symbol)
						value = l2->addr;
					else
						value = l2->op_value;
					l->op_value = value;
					l->label_resolved = 1;
					if (mii_cpu_op[l->opcodes[0]].desc.branch)
						l->op_value = l2->addr - l->addr - 2;
					else if (l->op_low)
						l->op_value &= 0xff;
					else if (l->op_high)
						l->op_value >>= 8;
					for (int i = 1; i < l->opcode_count; i++)
						l->opcodes[i] = l->op_value >> (8 * (i - 1));
					break;
				}
				l2 = l2->next;
			}
			if (!l->label_resolved) {
				printf("ERROR: Missing label %d: %s %s %s\n",
					l->line_index, l->label, l->mnemonic, l->operand);
				error = 1;
				break;
			}
		}
		l = l->next;
	}
	if (error)
		return error;
	p->output_len = p->prog_tail->addr + p->prog_tail->opcode_count - p->org;
	if (p->verbose)
		printf("%s program at $%04x size %d bytes\n", __func__,
					p->org, p->output_len);
	p->output = calloc(1, p->output_len);
	l = p->prog;
	while (l) {
		for (int i = 0; i < l->opcode_count; i++)
			p->output[l->addr - p->org + i] = l->opcodes[i];
		l = l->next;
	}
	return error;
}

int
mii_cpu_asm(
	mii_cpu_asm_program_t *p,
	const char *prog)
{
	mii_cpu_asm_load(p, prog);

	return mii_cpu_asm_assemble(p);
}

void
mii_cpu_asm_free(
	mii_cpu_asm_program_t *p)
{
	mii_cpu_asm_line_t *l = p->prog;
	while (l) {
		mii_cpu_asm_line_t *next = l->next;
		free(l);
		l = next;
	}
	if (p->output)
		free(p->output);
	memset(p, 0, sizeof(*p));
}
