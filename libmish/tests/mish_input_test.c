/*
 * This is a stress/scenario test for the mish_input_read function
 */
#define MISH_INPUT_TEST

#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

#include "mish.h"

static uint8_t *words = NULL;
static size_t words_size = 0;
static size_t words_offset = 0;

static int ccount = 0;

static ssize_t _test_read(int fd, void *w, size_t count) {
	errno = 0;

	if (!(ccount++ % 17)) {
		errno = EAGAIN;
		printf("%s fake EAGAIN\n", __func__);
		return -1;
	}

	if (words_offset + count > words_size)
		count = words_size - words_offset;

	memcpy(w, words + words_offset, count);
	words_offset += count;
	if (!count) {
		printf("%s all input sent\n", __func__);
	}
	return count;
}

#define read(_a,_b,_c) _test_read(_a,_b,_c)

#include "mish_input.c"
#include "mish_line.c"

#undef read

/* duplicate from mish_session.c */
/*
 * get a localtime equivalent, with milliseconds added
 */
uint64_t
_mish_stamp_ms()
{
	return 0;
}


int main()
{
	const char *filen = "/usr/share/dict/american-english";
	struct stat st;
	int fd;
	if ((lstat(filen, &st) == -1) || (fd = open(filen, O_RDONLY)) == -1) {
		perror(filen);
		exit(1);
	}
	words_size = st.st_size;
	words = malloc(words_size + 1);
	if (read(fd, words, st.st_size) != st.st_size) {
		perror("EOF unexpected");
		exit(1);
	}
	words[words_size] = 0;
	printf("File %s read\n", filen);

	/* That's not needed for the test, but init() relies on it */
	mish_t mish = {};
	mish_p m = &mish;
	FD_ZERO(&m->select.read);
	FD_ZERO(&m->select.write);
	TAILQ_INIT(&m->backlog.log);
	TAILQ_INIT(&m->clients);

	/* we don't need the file descriptor as we bypass "read" but the
	 * init function relies on it so here is a socketpair for it to play
	 * with */
	int sk[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sk)) {
		perror("socketpair");
		exit(1);
	}
	mish_input_t input = {};
	_mish_input_init(m, &input, sk[0]);

	uint8_t *cursor = words;
	int linec = 0;
	mish_line_p ln = NULL;

	/*
	 * Right so now we can actually test the function. We keep a running
	 * pointer on the source data, and the "last" line we compared, and
	 * compared line's content with the source buffer. If all goes
	 * well, they should match exactly.
	 */
	do {
		_mish_input_read(m, &m->select.read, &input);
		printf("  read up to o: %d/%d\n", (int)words_offset, (int)words_size);

		if (!ln)
			ln = TAILQ_FIRST(&input.backlog);
		else if (TAILQ_NEXT(ln, self))
			ln = TAILQ_NEXT(ln, self);
		else
			continue;
		while (ln) {
			linec++;
		//	printf("line %4d: %s", linec, ln->line);

			uint8_t * l = (uint8_t*)ln->line;
			for (int ic = 0; ic < ln->len; ic++, cursor++) {
				if (l[ic] != *cursor) {
					printf("ERROR: Mismatch Offset %ld char #%d/%d (%x/%x) line %d: %s\n",
						cursor - words, ic,
						ln->len,
						l[ic], *cursor,
						linec, l);
					goto done;
				}
			}

			if (TAILQ_LAST(&input.backlog, mish_line_queue_t) == ln)
				break;
			ln = TAILQ_NEXT(ln, self);
		}
	} while (words_offset < words_size);
	printf("%d lines were read and compared\n", linec);
done:
	exit(0);

}

