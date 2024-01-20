/*
 * mui_plugin.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

struct mui_t;
struct mui_drawable_t;

typedef struct mui_plug_t {
	void * (*init)(
					struct mui_t * ui,
					struct mui_plug_t * plug,
					struct mui_drawable_t * dr );
	void (*dispose)(
					void * plug );
	int (*draw)(
					struct mui_t *ui,
					void *param,
					struct mui_drawable_t * dr,
					uint16_t all );
	int (*event)(
					struct mui_t *ui,
					void *param,
					struct mui_event_t * event );
} mui_plug_t;
