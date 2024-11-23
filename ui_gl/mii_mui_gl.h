/*
 * mii_mui_gl.h
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "mii_mui.h"

void
mii_mui_gl_init(
		mii_mui_t *ui);
void
mii_mui_gl_prepare_textures(
		mii_mui_t *ui);
void
mui_mui_gl_regenerate_ui_texture(
		mii_mui_t *ui);
void
mii_mui_gl_render(
		mii_mui_t *ui);
bool
mii_mui_gl_run(
		mii_mui_t *ui);
