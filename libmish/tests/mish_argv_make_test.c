// small test unit for mish_argv_make
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


typedef struct _mish_argv_t {
	char * line;
	int ac;
	char * av[0];
} _mish_argv_t;
/*
 * Duplicate 'line', split it into words, store word pointers in an array,
 * NULL terminate it. Also return the number of words in the array in argc.
 *
 * The returned value is made of two malloc()ed blocks. use mish_argv_free
 * to free the memory.
 * It's OK to change any of the pointers. But no not try to realloc() the
 * vector as it hides a structure
 */
static char **
mish_argv_make(
		const char * line,
		int * argc )
{
	const char separator = ' ';
	_mish_argv_t * r = calloc(1, sizeof(*r));
	r->line = strdup(line);
	char *dup = r->line;
	char quote;
	enum { s_newarg = 0, s_startarg, s_copyquote, s_skip, s_copy };
	int state = s_newarg;
	do {
		switch (state) {
			case s_newarg:
				r = realloc(r, sizeof(*r) + ((r->ac + 2) * sizeof(char*)));
				while (*dup == ' ' || *dup == separator)
					dup++;
				r->av[r->ac++] = dup;
				state = s_startarg;
				break;
			case s_startarg:
				if (*dup == '"' || *dup == '\'') {
					quote = *dup++;
					state = s_copyquote;
				} else
					state = s_copy;
				break;
			case s_copyquote:
				if (*dup == '\\') {
					state = s_skip;
					dup++;
				} else if (*dup == quote) {
					state = s_newarg;
					dup++;
					if (*dup) *dup++ = 0;
				} else if (*dup)
					dup++;
				break;
			case s_skip:
				dup++;
				state = s_copyquote;
				break;
			case s_copy:
				if (*dup == 0)
					break;
				if (*dup != separator)
					dup++;
				else {
					state = s_newarg;
					if (*dup) *dup++ = 0;
				}
				break;
		}
	} while (*dup);
	r->av[r->ac] = NULL;
	if (argc)
		*argc = r->ac;
	return r->av;
}

int main() {
	int argc = 0;
	char **argv = mish_argv_make("testing \"one escape two\"  lala ", &argc);

	printf("argc = %d\n", argc);
	for (int i = 0; argv[i]; i++)
		printf("%2d:'%s'\n", i, argv[i]);

	argv = mish_argv_make("command with some \" quoted\\\"words \" should work\n", &argc);

	printf("argc = %d\n", argc);
	for (int i = 0; argv[i]; i++)
		printf("%2d:'%s'\n", i, argv[i]);

}
