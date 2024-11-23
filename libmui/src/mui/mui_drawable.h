/*
 * mui_drawable.h
 *
 * Copyright (C) 2024 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <mui/mui_types.h>
#include <pixman.h>


struct cg_surface_t;
struct cg_ctx_t;

/*
 * Describes a pixmap.
 * And really, only bpp:32 for ARGB is supported if you want to use 'cg' to draw
 * on it,
 * 8bpp is also used for alpha masks, in which case only the pixman API is used.
 * (Alpha mask is used for text rendering)
 */
typedef struct mui_pixmap_t {
	uint8_t * 					pixels;
	uint32_t 					bpp : 8;
	c2_pt_t 					size;
	uint32_t 					row_bytes;
} mui_pixmap_t;

typedef pixman_region32_t 	mui_region_t;

DECLARE_C_ARRAY(mui_region_t, mui_clip_stack, 2);

/*
  ██████  ██████   █████  ██     ██  █████  ██████  ██      ███████
  ██   ██ ██   ██ ██   ██ ██     ██ ██   ██ ██   ██ ██      ██
  ██   ██ ██████  ███████ ██  █  ██ ███████ ██████  ██      █████
  ██   ██ ██   ██ ██   ██ ██ ███ ██ ██   ██ ██   ██ ██      ██
  ██████  ██   ██ ██   ██  ███ ███  ██   ██ ██████  ███████ ███████
*/
/*
 * The Drawable is a drawing context. The important feature
 * of this is that it keeps a context for the pixman library destination
 * image, AND also the context for the 'cg' vectorial library.
 *
 * Furthermore it keeps track of a stack of clipping rectangles, and is able
 * to 'sync' the current clipping area for either (or both) cg and libpixman.
 *
 * Important note: the cg vectorial library coordinate system is placed on the
 * space *between* pixels, ie, if you moveto(1,1) and draw a line down, you
 * will light up pixels in columns zero AND one (at half transparency).
 * This differs significantly from for example, pixman that is uses pixel
 * coordinates on hard pixels.
 *
 * It's worth remembering as if you draw for example around the border of a
 * control, it will very likely be 'clipped' somewhat because half the pixels
 * are technically outside the control bounding/clipping rectangle.
 * You can easily adjust for this by adding 0.5 to the coordinates, if you
 * require it.
 *
 * Other imporant note: The clipping stack is only converted to pixman/cg when
 * the client code asks for the context. So you must make sure not to 'cache'
 * the context too early, otherwise the clipping won't work.
 * Bad:
 * 	struct cg_t * cg = mui_drawable_get_cg(dr);
 * 	mui_drawable_clip_push(dr, &r);
 * 	...
 * 	mui_drawable_clip_pop(dr);
 * Good:
 * 	mui_drawable_clip_push(dr, &r);
 * 	struct cg_t * cg = mui_drawable_get_cg(dr);
 * 	...
 * 	mui_drawable_clip_pop(dr);
 */
typedef struct mui_drawable_t {
	mui_pixmap_t				pix;	// *has* to be first in struct
	void * 						_pix_hash; // used to detect if pix has changed
	struct cg_surface_t * 		cg_surface;
	struct cg_ctx_t *			cg;		// Do not to use these directly
	union pixman_image *		pixman;	// Do not to use these directly
	uint						pixman_clip_dirty: 1,
								cg_clip_dirty : 1,
								dispose_pixels : 1,
								dispose_drawable : 1;
	// not used internally, but useful for the application
	struct {
		float 						opacity;
		c2_pt_t 					size;
		uint						id, kind;
	}							texture;
	// (default) position in destination when drawing (optional)
	c2_pt_t 					origin;
	mui_clip_stack_t			clip;
} mui_drawable_t;

// Use IMPLEMENT_C_ARRAY(mui_drawable_array); if you need this
DECLARE_C_ARRAY(mui_drawable_t *, mui_drawable_array, 4);

/*
 * Drawable related
 */
/* create a new mui_drawable of size w x h, bpp depth.
 * Optionally allocate the pixels if pixels is NULL. Allocated pixels
 * are not cleared to white/zero. */
mui_drawable_t *
mui_drawable_new(
		c2_pt_t 		size,
		uint8_t 		bpp,
		void * 			pixels, // if NULL, will allocate
		uint32_t 		row_bytes);
/* initialize a mui_drawable_t structure with the given parameters
 * note it is not assumed 'd' contains anything valid, it will be
 * overwritten */
mui_drawable_t *
mui_drawable_init(
		mui_drawable_t * d,
		c2_pt_t 		size,
		uint8_t 		bpp,
		void * 			pixels, // if NULL, will allocate
		uint32_t 		row_bytes);
void
mui_drawable_dispose(
		mui_drawable_t * dr);
// Clear, but do not dispose of the drawable
void
mui_drawable_clear(
		mui_drawable_t * dr);

// get/allocate a pixman structure for this drawable
union pixman_image *
mui_drawable_get_pixman(
		mui_drawable_t * dr);
// get/allocate a cg drawing context for this
struct cg_ctx_t *
mui_drawable_get_cg(
		mui_drawable_t * dr);
// return 0 (no intersect), 1: fully contained and 2: partial contains
int
mui_drawable_clip_intersects(
		mui_drawable_t * dr,
		c2_rect_p r );
void
mui_drawable_set_clip(
		mui_drawable_t * dr,
		c2_rect_array_p clip );
int
mui_drawable_clip_push(
		mui_drawable_t * dr,
		c2_rect_p 		r );
int
mui_drawable_clip_push_region(
		mui_drawable_t * dr,
		pixman_region32_t * rgn );
int
mui_drawable_clip_substract_region(
		mui_drawable_t * dr,
		pixman_region32_t * rgn );
void
mui_drawable_clip_pop(
		mui_drawable_t * dr );
pixman_region32_t *
mui_drawable_clip_get(
		mui_drawable_t * dr);
void
mui_drawable_resize(
		mui_drawable_t * dr,
		c2_pt_t size);

/*
 * Your typical ARGB color. Note that the components are NOT
 * alpha-premultiplied at this stage.
 * This struct should be able to be passed as a value, not a pointer
 */
typedef union mui_color_t {
	struct {
		uint8_t a,r,g,b;
	} __attribute__((packed));
	uint32_t value;
	uint8_t v[4];
} mui_color_t;

typedef struct mui_control_color_t {
	mui_color_t fill, frame, text;
} mui_control_color_t;

#define MUI_COLOR(_v) ((mui_color_t){ .value = (_v)})

#define CG_COLOR(_c) (struct cg_color_t){ \
			.a = (_c).a / 255.0, .r = (_c).r / 255.0, \
			.g = (_c).g / 255.0, .b = (_c).b / 255.0 }
/*
 * Pixman use premultiplied alpha values
 */
#define PIXMAN_COLOR(_c) (pixman_color_t){ \
			.alpha = (_c).a * 257, .red = (_c).r * (_c).a, \
			.green = (_c).g * (_c).a, .blue = (_c).b * (_c).a }
