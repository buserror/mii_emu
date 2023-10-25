/*
 * minipt.h
 *
 * Created on: 1 Apr 2018
 * Copyright (C) 2020 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MPTOOLS_INCLUDE_MINIPT_H_
#define MPTOOLS_INCLUDE_MINIPT_H_

/*
 * Mini Protothread.
 *
 * A thread or a coroutine would use a stack; this won't,
 * Use an old gcc trick of being able to goto and indirect label.
 * There are a few caveats: no persistent local variables, as you can't
 * have a consistent stack frame. It's easy to work around tho.
 */
#define _CONCAT2(s1, s2) s1##s2
#define _CONCAT(s1, s2) _CONCAT2(s1, s2)

/* this wierd thing with the union is for gcc 12, which doesn't like us
 * storing the address of a 'local variable' (which is the label!) */
static inline void _set_gcc_ptr_workaround(void **d, void *s) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
	*d = s;
#pragma GCC diagnostic pop
}
#define pt_start(_pt) do { \
		if (_pt) goto *_pt; \
	} while (0);
#define pt_end(_pt) do { \
		(_pt) = NULL; \
		_pt_exit: ; \
	} while(0);
#define pt_yield(_pt) do { \
		_set_gcc_ptr_workaround(&(_pt), &&_CONCAT(_label, __LINE__));\
		goto _pt_exit;\
		_CONCAT(_label, __LINE__): ; \
	} while (0);

#define pt_wait(_pt, _condition) do { \
		while (!(_condition)) \
			pt_yield(_pt); \
	} while (0);


#ifdef NEVER
/*
 * This version is superior as it allows calling functions and keeping
 * a context, but I never actually had a /need/ for this, yet
 */

struct pt_t {
	unsigned int sp;
	void * st[32];
	void * ctx[32];
} pt_t;

#define pt_start(_pt) do { \
		if ((_pt)->st[(_pt)->sp]) goto *((_pt)->st[(_pt)->sp]); \
	} while (0);
#define pt_end(_pt) do { (_pt)->st[(_pt)->sp] = NULL; return; } while(0);
#define pt_yield(_pt) do { \
		(_pt)->st[(_pt)->sp] = &&_CONCAT(_label, __LINE__);\
		return;\
		_CONCAT(_label, __LINE__): ; \
	} while (0);
#define pt_wait(_pt, _condition) do { \
		while (!(_condition)) \
			pt_yield(_pt); \
	} while (0);
#define pt_call(_pt, _func) do { \
		(_pt)->sp++; \
		(_pt)->st[(_pt)->sp] = NULL; \
		do { \
			_func(_pt); \
		} while ((_pt)->st[(_pt)->sp]); \
		(_pt)->sp--; \
	} while (0);
#define pt_ctx(_pt) ((_pt)->ctx[(_pt)->sp])

void my_minit_func(struct pt_t * p) {
	pt_start(p);
	pt_ctx(p) = calloc(1, sizeof(int));
	printf("%s start %p\n", __func__, pt_ctx(p));
	pt_yield(p);
	int * ctx = pt_ctx(p);
	printf("%s loop %p\n", __func__, pt_ctx(p));
	for (; *ctx < 10; ctx[0]++) {
		printf("   loop %d\n", *ctx);
		pt_yield(p);
		ctx = pt_ctx(p);
	}
	printf("%s done %p\n", __func__, pt_ctx(p));
	free(pt_ctx(p));
	pt_ctx(p) = NULL;
	pt_end(p);
}

void my_minit(struct pt_t * p) {
	pt_start(p);
	printf("%s start\n", __func__);
	pt_call(p, my_minit_func);
	printf("%s done\n", __func__);
	pt_end(p);
}

int main() {
	struct pt_t pt = {};

	pt_call(&pt, my_minit);
}
/*
tcc -run pt_call_test.c
my_minit start
my_minit_func start 0x555a68d970b0
my_minit_func loop 0x555a68d970b0
   loop 0
   loop 1
   loop 2
   loop 3
   loop 4
   loop 5
   loop 6
   loop 7
   loop 8
   loop 9
my_minit_func done 0x555a68d970b0
my_minit done
*/

#endif // NEVER

#endif /* MPTOOLS_INCLUDE_MINIPT_H_ */
