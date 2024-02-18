/*
 * mii_loadbin.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <pthread.h>

#include "mii.h"
#include "mii_bank.h"
#include "mii_mui_settings.h"


typedef struct mii_loadbin_t {
	mii_t *					mii;
	pthread_t				thread;

	mii_loadbin_conf_t 		conf;
} mii_loadbin_t;

static mii_loadbin_t * _mii_loadbin = NULL;

static void *
mii_thread_loadbin(
		void *arg)
{

	return NULL;
}

mii_loadbin_t *
mii_loadbin_start(
		struct mii_t *mii,
		struct mii_loadbin_conf_t *conf)
{
	mii_loadbin_t * res = NULL;
	if (_mii_loadbin) {
		return _mii_loadbin;
	}
	_mii_loadbin = res = calloc(1, sizeof(*res));
	res->mii = mii;
	res->conf = *conf;

	pthread_create(&res->thread, NULL, mii_thread_loadbin, res);

	return res;
}
