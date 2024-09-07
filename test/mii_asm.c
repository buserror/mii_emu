/*
 * mii_asm.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror+git@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 *
 * Small command line assembler for our own drivers
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "mii_65c02.c"
#include "mii_65c02_asm.c"
#include "mii_65c02_disasm.c"
#include "mii_65c02_ops.h"

int
main(
		int argc,
		const char * argv[])
{
	const char *infile = NULL;
	const char *outfile = NULL;
	int verbose = 0;
	// parse arguments
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-o")) {
			if (i + 1 >= argc) {
				fprintf(stderr, "%s Missing argument for -o\n", argv[0]);
				exit(1);
			}
			outfile = argv[++i];
		} else if (!strcmp(argv[i], "-v")) {
			verbose = 1;
		} else if (!infile) {
			infile = argv[i];
		} else {
			fprintf(stderr, "%s Unknown argument %s\n", argv[0], argv[i]);
			exit(1);
		}
	}
	if (!infile || !outfile) {
		fprintf(stderr, "Usage: mii_asm [-v] -o outfile infile\n");
		exit(1);
	}
	char *prog = calloc(1, 65536);
	if (verbose)
		printf("%s Loading %s\n", argv[0], infile);
	FILE *f = fopen(infile, "r");
	if (!f) {
		perror(infile);
		exit(1);

	}
	fread(prog, 1, 65536, f);
	fclose(f);

	mii_cpu_asm_program_t p = {};
	p.verbose = verbose;
	if (mii_cpu_asm(&p, prog) == 0) {
		if (verbose)
			mii_cpu_disasm(p.output, p.org, p.output_len);
		if (verbose)
			printf("%s Writing %s\n", argv[0], outfile);
		f = fopen(outfile, "w");
		if (!f) {
			perror(outfile);
			exit(1);
		}
		fwrite(p.output, 1, p.output_len, f);
		fclose(f);
	} else {
		fprintf(stderr, "%s: %s error, no output\n", argv[0], infile);
		exit(1);
	}

	return 0;
}
