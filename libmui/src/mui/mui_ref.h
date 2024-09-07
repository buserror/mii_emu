/*
 * mui_ref.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>

struct mui_t;
struct mui_control_t;
struct mui_window_t;

/*!
 * References allows arbitrary code to keep a 'handle' on either
 * a window or a control. This is used for example to keep track of
 * the currently focused control.
 * Client code Must Not keep random pointers on control and windows,
 * as they could get deleted and they will end up with a dangling
 * pointer.
 * Instead, client code create a reference, and use that reference
 * to keep track of the object. If an object is deleted, all it's
 * current references are reset to NULL, so the client code can
 * detect that the object is gone just by checking that its pointer
 * is still around. Otherwise, it's just gone.
 */
struct mui_ref_t;

typedef struct mui_refqueue_t {
	TAILQ_HEAD(head, mui_ref_t) 	head;
} mui_refqueue_t;

typedef void (*mui_deref_p)(
			struct mui_ref_t *		ref);

typedef struct mui_ref_t {
	// in refqueue's 'head'
	TAILQ_ENTRY(mui_ref_t) 		self;
	mui_refqueue_t * 			queue;
	// OPTIONAL arbitrary kind set when referencing an object.
	uint32_t 					kind;
	uint32_t 					alloc : 1, trace : 1, count : 8;
	// OPTIONAL: called if the object win/control get disposed or
	// otherwise dereferenced.
	mui_deref_p					 deref;
} _mui_ref_t;	// this is not a 'user' type.

/*
 * Window and Control references
 * While these two count technically be a union, I've deciced for separate
 * types to enforce the type checking.
 */
typedef struct mui_window_ref_t {
	_mui_ref_t 					ref;
	struct mui_window_t * 		window;
} mui_window_ref_t;

typedef struct mui_control_ref_t {
	_mui_ref_t 					ref;
	struct mui_control_t * 		control;
} mui_control_ref_t;

/*!
 * Initializes a reference to 'control', with the (optional) kind.
 * if 'ref' is NULL a new reference is allocated and returned, will be
 * freed on deref().
 * 'kind' is an optional arbitrary value that can be used to identify
 * the reference, it has no meaning to the library.
 */
mui_control_ref_t *
mui_control_ref(
		mui_control_ref_t *		ref,
		struct mui_control_t *	control,
		uint32_t 				kind);
void
mui_control_deref(
		mui_control_ref_t *		ref);
/*!
 * Initializes a reference to 'window', with the (optional) kind.
 * if 'ref' is NULL a new reference is allocated and returned, will be
 * freed on deref().
 * 'kind' is an optional arbitrary value that can be used to identify
 * the reference, it has no meaning to the library.
 */
mui_window_ref_t *
mui_window_ref(
		mui_window_ref_t *		ref,
		struct mui_window_t * 	win,
		uint32_t 				kind);
void
mui_window_deref(
		mui_window_ref_t *		ref);
