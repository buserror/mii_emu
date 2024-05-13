/*
 * mii_vcd.c
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include "mii.h"
#include "mii_vcd.h"

DEFINE_FIFO(mii_vcd_log_t, mii_vcd_fifo);

#define strdupa(__s) strcpy(alloca(strlen(__s)+1), __s)

static void
_mii_vcd_notify(
		struct mii_signal_t * sig,
		uint32_t value,
		void * param);

int
mii_vcd_init(
		struct mii_t * mii,
		const char * filename,
		mii_vcd_t * vcd,
		uint32_t cycle_to_nsec)
{
	memset(vcd, 0, sizeof(mii_vcd_t));
	vcd->mii 		= mii;
	vcd->filename 	= strdup(filename);
	vcd->cycle_to_nsec 	= cycle_to_nsec; // mii_usec_to_cycles(vcd->mii, period);
	return 0;
}

void
mii_vcd_close(
		mii_vcd_t * vcd)
{
	mii_vcd_stop(vcd);

	/* dispose of any link and hooks */
	for (int i = 0; i < vcd->signal_count; i++) {
		mii_vcd_signal_t * s = &vcd->signal[i];

		mii_free_signal(&s->sig, 1);
	}
	if (vcd->filename) {
		free(vcd->filename);
		vcd->filename = NULL;
	}
}


static char *
_mii_vcd_get_float_signal_text(
		mii_vcd_signal_t * s,
		char * out)
{
	char * dst = out;

	if (s->size > 1)
		*dst++ = 'b';

	for (int i = s->size; i > 0; i--)
		*dst++ = 'x';
	if (s->size > 1)
		*dst++ = ' ';
	*dst++ = s->alias;
	*dst = 0;
	return out;
}

static char *
_mii_vcd_get_signal_text(
		mii_vcd_signal_t * s,
		char * out,
		uint32_t value)
{
	char * dst = out;

	if (s->size > 1)
		*dst++ = 'b';

	for (int i = s->size; i > 0; i--)
		*dst++ = value & (1 << (i-1)) ? '1' : '0';
	if (s->size > 1)
		*dst++ = ' ';
	*dst++ = s->alias;
	*dst = 0;
	return out;
}

#define mii_cycles_to_nsec(mii, c) ((c) * vcd->cycle_to_nsec)

/* Write queued output to the VCD file. */

static void
mii_vcd_flush_log(
		mii_vcd_t * vcd)
{
	uint64_t seen = 0;
	uint64_t oldbase = 0;	// make sure it's different
	char out[256];

	if (mii_vcd_fifo_isempty(&vcd->log) || !vcd->output)
		return;

	while (!mii_vcd_fifo_isempty(&vcd->log)) {
		mii_vcd_log_t l = mii_vcd_fifo_read(&vcd->log);
		// 10ns base -- 100MHz should be enough
		uint64_t base = mii_cycles_to_nsec(vcd->mii, l.when - vcd->start) / 10;

		/*
		 * if that trace was seen in this nsec already, we fudge the
		 * base time to make sure the new value is offset by one nsec,
		 * to make sure we get at least a small pulse on the waveform.
		 *
		 * This is a bit of a fudge, but it is the only way to represent
		 * very short "pulses" that are still visible on the waveform.
		 */
		if (base == oldbase &&
				(seen & (1 << l.sigindex)))
			base++;	// this forces a new timestamp

		if (base > oldbase || !seen) {
			seen = 0;
			fprintf(vcd->output, "#%" PRIu64  "\n", base);
			oldbase = base;
		}
		// mark this trace as seen for this timestamp
		seen |= (1 << l.sigindex);
		fprintf(vcd->output, "%s\n",
				l.floating ?
					_mii_vcd_get_float_signal_text(
							&vcd->signal[l.sigindex],
							out) :
					_mii_vcd_get_signal_text(
							&vcd->signal[l.sigindex],
							out, l.value));
	}
}


/* Called for an IRQ that is being recorded. */

static void
_mii_vcd_notify(
		struct mii_signal_t * sig,
		uint32_t value,
		void * param)
{
	mii_vcd_t * vcd = (mii_vcd_t *)param;

	if (!vcd->output) {
		printf("%s: no output\n",
				__func__);
		return;
	}
	mii_vcd_signal_t * s = (mii_vcd_signal_t*)sig;
	mii_vcd_log_t l = {
		.sigindex = s->sig.sig,
		.when = vcd->cycle,
		.value = value,
		.floating = !!(mii_signal_get_flags(sig) & SIG_FLAG_FLOATING),
	};
	if (mii_vcd_fifo_isfull(&vcd->log)) {
	//	printf("%s FIFO Overload, flushing!\n", __func__);
		/* Decrease period by a quarter, for next time */
	//	vcd->period -= vcd->period >> 2;
		mii_vcd_flush_log(vcd);
	}
	mii_vcd_fifo_write(&vcd->log, l);
}

/* Register an IRQ whose value is to be logged. */

int
mii_vcd_add_signal(
		mii_vcd_t * vcd,
		mii_signal_t * signal_sig,
		uint signal_bit_size,
		const char * name )
{
	if (vcd->signal_count == MII_VCD_MAX_SIGNALS) {
		printf(" %s: unable add signal '%s'\n", __func__, name);
		return -1;
	}
	int index = vcd->signal_count++;
	mii_vcd_signal_t * s = &vcd->signal[index];
	strncpy(s->name, name, sizeof(s->name));
	s->size = signal_bit_size;
	s->alias = ' ' + vcd->signal_count ;

	/* manufacture a nice IRQ name */
	int l = strlen(name);
	char iname[10 + l + 1];
	if (signal_bit_size > 1)
		sprintf(iname, "%d>vcd.%s", signal_bit_size, name);
	else
		sprintf(iname, ">vcd.%s", name);

	const char * names[1] = { iname };
	mii_init_signal(&vcd->mii->sig_pool, &s->sig, index, 1, names);
	mii_signal_register_notify(&s->sig, _mii_vcd_notify, vcd);

	mii_connect_signal(signal_sig, &s->sig);
	return 0;
}

/* Open the VCD output file and write header.  Does nothing for input. */

int
mii_vcd_start(
		mii_vcd_t * vcd)
{
	time_t now;

	vcd->start = 0;
	mii_vcd_fifo_reset(&vcd->log);

	if (vcd->output)
		mii_vcd_stop(vcd);
	vcd->output = fopen(vcd->filename, "w");
	if (vcd->output == NULL) {
		perror(vcd->filename);
		return -1;
	}

	time(&now);
	fprintf(vcd->output, "$date %s$end\n", ctime(&now));
	fprintf(vcd->output,
		"$version Simmii " "1.0.0" " $end\n");
	fprintf(vcd->output, "$timescale 10ns $end\n");	// 10ns base, aka 100MHz
	fprintf(vcd->output, "$scope module logic $end\n");

	for (int i = 0; i < vcd->signal_count; i++) {
		fprintf(vcd->output, "$var wire %d %c %s $end\n",
			vcd->signal[i].size, vcd->signal[i].alias, vcd->signal[i].name);
	}

	fprintf(vcd->output, "$upscope $end\n");
	fprintf(vcd->output, "$enddefinitions $end\n");

	fprintf(vcd->output, "$dumpvars\n");
	for (int i = 0; i < vcd->signal_count; i++) {
		mii_vcd_signal_t * s = &vcd->signal[i];
		char out[48];
		fprintf(vcd->output, "%s\n",
				_mii_vcd_get_float_signal_text(s, out));
	}
	fprintf(vcd->output, "$end\n");
//	mii_cycle_timer_register(vcd->mii, vcd->period, _mii_vcd_timer, vcd);
	return 0;
}

int
mii_vcd_stop(
		mii_vcd_t * vcd)
{
//	mii_cycle_timer_cancel(vcd->mii, _mii_vcd_timer, vcd);

	mii_vcd_flush_log(vcd);

	if (vcd->output)
		fclose(vcd->output);
	vcd->output = NULL;
	return 0;
}



// internal structure for a hook, never seen by the notify procs
typedef struct mii_signal_hook_t {
	struct mii_signal_hook_t * next;
	int busy;	// prevent reentrance of callbacks

	struct mii_signal_t * chain;	// raise the IRQ on this too - optional if "notify" is on
	mii_signal_notify_t notify;	// called when IRQ is raised - optional if "chain" is on
	void * param;				// "notify" parameter
} mii_signal_hook_t;

static void
_mii_signal_pool_add(
		mii_signal_pool_t * pool,
		mii_signal_t * sig)
{
	uint insert = 0;
	/* lookup a slot */
	for (; insert < pool->count && pool->sig[insert]; insert++)
		;
	if (insert == pool->count) {
		if ((pool->count & 0xf) == 0) {
			pool->sig = (mii_signal_t**)realloc(pool->sig,
					(pool->count + 16) * sizeof(mii_signal_t *));
		}
		pool->count++;
	}
	pool->sig[insert] = sig;
	sig->pool = pool;
}

static void
_mii_signal_pool_remove(
		mii_signal_pool_t * pool,
		mii_signal_t * sig)
{
	for (uint i = 0; i < pool->count; i++)
		if (pool->sig[i] == sig) {
			pool->sig[i] = 0;
			return;
		}
}

void
mii_init_signal(
		mii_signal_pool_t * pool,
		mii_signal_t * sig,
		uint32_t base,
		uint32_t count,
		const char ** names /* optional */)
{
	memset(sig, 0, sizeof(mii_signal_t) * count);

	for (uint i = 0; i < count; i++) {
		sig[i].sig = base + i;
		sig[i].flags = SIG_FLAG_INIT;
		if (pool)
			_mii_signal_pool_add(pool, &sig[i]);
		if (names && names[i])
			sig[i].name = strdup(names[i]);
		else {
			printf("WARNING %s() with NULL name for sig %d.\n",
					__func__, sig[i].sig);
		}
	}
}

mii_signal_t *
mii_alloc_signal(
		mii_signal_pool_t * pool,
		uint32_t base,
		uint32_t count,
		const char ** names /* optional */)
{
	mii_signal_t * sig = (mii_signal_t*)malloc(sizeof(mii_signal_t) * count);
	mii_init_signal(pool, sig, base, count, names);
	for (uint i = 0; i < count; i++)
		sig[i].flags |= SIG_FLAG_ALLOC;
	return sig;
}

static mii_signal_hook_t *
_mii_alloc_signal_hook(
		mii_signal_t * sig)
{
	mii_signal_hook_t *hook = malloc(sizeof(mii_signal_hook_t));
	memset(hook, 0, sizeof(mii_signal_hook_t));
	hook->next = sig->hook;
	sig->hook = hook;
	return hook;
}

void
mii_free_signal(
		mii_signal_t * sig,
		uint32_t count)
{
	if (!sig || !count)
		return;
	for (uint i = 0; i < count; i++) {
		mii_signal_t * iq = sig + i;
		if (iq->pool)
			_mii_signal_pool_remove(iq->pool, iq);
		if (iq->name)
			free((char*)iq->name);
		iq->name = NULL;
		// purge hooks
		mii_signal_hook_t *hook = iq->hook;
		while (hook) {
			mii_signal_hook_t * next = hook->next;
			free(hook);
			hook = next;
		}
		iq->hook = NULL;
	}
	// if that sig list was allocated by us, free it
	if (sig->flags & SIG_FLAG_ALLOC)
		free(sig);
}

void
mii_signal_register_notify(
		mii_signal_t * sig,
		mii_signal_notify_t notify,
		void * param)
{
	if (!sig || !notify)
		return;

	mii_signal_hook_t *hook = sig->hook;
	while (hook) {
		if (hook->notify == notify && hook->param == param)
			return;	// already there
		hook = hook->next;
	}
	hook = _mii_alloc_signal_hook(sig);
	hook->notify = notify;
	hook->param = param;
}

void
mii_signal_unregister_notify(
		mii_signal_t * sig,
		mii_signal_notify_t notify,
		void * param)
{
	mii_signal_hook_t *hook, *prev;
	if (!sig || !notify)
		return;

	hook = sig->hook;
	prev = NULL;
	while (hook) {
		if (hook->notify == notify && hook->param == param) {
			if ( prev )
				prev->next = hook->next;
			else
				sig->hook = hook->next;
			free(hook);
			return;
		}
		prev = hook;
		hook = hook->next;
	}
}

void
mii_raise_signal_float(
		mii_signal_t * sig,
		uint32_t value,
		int floating)
{
	if (!sig)
		return ;
	uint32_t output = (sig->flags & SIG_FLAG_NOT) ? !value : value;
	// if value is the same but it's the first time, raise it anyway
	if (sig->value == output &&
			(sig->flags & SIG_FLAG_FILTERED) && !(sig->flags & SIG_FLAG_INIT))
		return;
	sig->flags &= ~(SIG_FLAG_INIT | SIG_FLAG_FLOATING);
	if (floating)
		sig->flags |= SIG_FLAG_FLOATING;
	mii_signal_hook_t *hook = sig->hook;
	while (hook) {
		mii_signal_hook_t * next = hook->next;
			// prevents reentrance / endless calling loops
		if (hook->busy == 0) {
			hook->busy++;
			if (hook->notify)
				hook->notify(sig, output,  hook->param);
			if (hook->chain)
				mii_raise_signal_float(hook->chain, output, floating);
			hook->busy--;
		}
		hook = next;
	}
	// the value is set after the callbacks are called, so the callbacks
	// can themselves compare for old/new values between their parameter
	// they are passed (new value) and the previous sig->value
	sig->value = output;
}

void
mii_raise_signal(
		mii_signal_t * sig,
		uint32_t value)
{
	mii_raise_signal_float(sig, value, !!(sig->flags & SIG_FLAG_FLOATING));
}

void
mii_connect_signal(
		mii_signal_t * src,
		mii_signal_t * dst)
{
	if (!src || !dst || src == dst) {
		fprintf(stderr, "error: %s invalid sig %p/%p", __func__, src, dst);
		return;
	}
	mii_signal_hook_t *hook = src->hook;
	while (hook) {
		if (hook->chain == dst)
			return;	// already there
		hook = hook->next;
	}
	hook = _mii_alloc_signal_hook(src);
	hook->chain = dst;
}

void
mii_unconnect_signal(
		mii_signal_t * src,
		mii_signal_t * dst)
{
	mii_signal_hook_t *hook, *prev;

	if (!src || !dst || src == dst) {
		fprintf(stderr, "error: %s invalid sig %p/%p", __func__, src, dst);
		return;
	}
	hook = src->hook;
	prev = NULL;
	while (hook) {
		if (hook->chain == dst) {
			if ( prev )
				prev->next = hook->next;
			else
				src->hook = hook->next;
			free(hook);
			return;
		}
		prev = hook;
		hook = hook->next;
	}
}

uint8_t
mii_signal_get_flags(
		mii_signal_t * sig )
{
	return sig->flags;
}

void
mii_signal_set_flags(
		mii_signal_t * sig,
		uint8_t flags )
{
	sig->flags = flags;
}
