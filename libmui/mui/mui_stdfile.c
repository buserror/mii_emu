/*
 * mui_stdfile.c
 *
 * Copyright (C) 2023 Michel Pollet <buserror@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <regex.h>
#include <errno.h>
#include <sys/stat.h>
#include <glob.h>
#include <libgen.h>

#include "mui.h"
#include "c2_geometry.h"

#include <sys/types.h>
#include <dirent.h>

DECLARE_C_ARRAY(char*, string_array, 2);
IMPLEMENT_C_ARRAY(string_array);

typedef struct mui_stdfile_t {
	mui_window_t 		win;
	mui_control_t *		ok, *cancel, *home, *root;
	mui_control_t *		listbox, *popup;
	char * 				pref_file; // pathname we put last path used
	char * 				re_pattern;
	char *				current_path;
	char *				selected_path;
	regex_t 			re;
	string_array_t		pop_path;
} mui_stdfile_t;

enum {
	MUI_STD_FILE_PART_FRAME = 0,
	MUI_STD_FILE_PART_OK,
	MUI_STD_FILE_PART_CANCEL,
	MUI_STD_FILE_PART_HOME,
	MUI_STD_FILE_PART_ROOT,
	MUI_STD_FILE_PART_LISTBOX,
	MUI_STD_FILE_PART_POPUP,
	MUI_STD_FILE_PART_COUNT,
};

static int
_mui_stdfile_sort_cb(
		const void * a,
		const void * b)
{
	const mui_listbox_elem_t * ea = a;
	const mui_listbox_elem_t * eb = b;
	#if 0
	if (ea->icon == MUI_ICON_FOLDER && eb->icon != MUI_ICON_FOLDER)
		return -1;
	if (ea->icon != MUI_ICON_FOLDER && eb->icon == MUI_ICON_FOLDER)
		return 1;
	#endif
	return strcmp(ea->elem, eb->elem);
}

static int
_mui_stdfile_populate(
	mui_stdfile_t * std,
	const char * path)
{
	if (std->current_path && !strcmp(std->current_path, path))
		return 0;

	printf("%s %s\n", __func__, path);
	errno = 0;
	DIR * dir = opendir(path);
	if (!dir) {
		// show an alert of some sort
		char * msg = NULL;
		asprintf(&msg, "%s\n%s", path,
					strerror(errno));
		mui_alert(std->win.ui, C2_PT(0,0),
					"Could not open directory",
					msg,
					MUI_ALERT_FLAG_OK);
		return -1;
	}
	if (std->current_path)
		free(std->current_path);
	std->current_path = strdup(path);
	path = NULL; // this COULD be in the list we are now deleting!
	for (int i = 0; i < (int)std->pop_path.count; i++)
		free(std->pop_path.e[i]);
	std->pop_path.count = 0;

	mui_control_t *pop = std->popup;
	mui_menu_items_t * items = mui_popupmenu_get_items(pop);
	mui_menu_items_clear(items);
	char * p = strdup(std->current_path);
	char * d;
	const char *home = getenv("HOME");
	int item_id = 1000;
	while ((d = basename(p)) != NULL) {
		mui_menu_item_t i = {
			.title = strdup(d),
			.uid = item_id++,
		};
		if (!strcmp(d, "/"))
			strcpy(i.icon, MUI_ICON_ROOT);
		else if (home && !strcmp(p, home))
			strcpy(i.icon, MUI_ICON_HOME);
		else
			strcpy(i.icon, MUI_ICON_FOLDER_OPEN);
		mui_menu_items_push(items, i);
	//	printf(" %s - %s\n", p, d);
		string_array_push(&std->pop_path, strdup(p));
		if (!strcmp(d, "/"))
			break;
		*d = 0;
	}
	free(p);
	mui_menu_item_t z = {};
	mui_menu_items_push(items, z);
	mui_popupmenu_prepare(pop);

	mui_control_t * lb = std->listbox;
	mui_listbox_elems_t * elems = mui_listbox_get_elems(lb);
	mui_listbox_elems_clear(elems);
	struct dirent * ent;
	while ((ent = readdir(dir))) {
		if (ent->d_name[0] == '.')
			continue;
		struct stat st;
		char * full_path = NULL;
		asprintf(&full_path, "%s/%s", std->current_path, ent->d_name);
		stat(full_path, &st);
		free(full_path);
		mui_listbox_elem_t e = {};
		// usr the regex to filter file names
		if (std->re_pattern) {
			if (!S_ISDIR(st.st_mode) && regexec(&std->re, ent->d_name, 0, NULL, 0))
				e.disabled = 1;
		}
		e.elem = strdup(ent->d_name);
		if (S_ISDIR(st.st_mode))
			strcpy(e.icon, MUI_ICON_FOLDER);
		else
			strcpy(e.icon, MUI_ICON_FILE);
		mui_listbox_elems_push(elems, e);
	}
	qsort(elems->e, elems->count,
			sizeof(mui_listbox_elem_t), _mui_stdfile_sort_cb);
	mui_control_set_value(lb, 0);
	mui_listbox_prepare(lb);
	closedir(dir);
	return 0;
}

static int
_mui_stdfile_window_action(
		mui_window_t * 	win,
		void * 			cb_param,
		uint32_t 		what,
		void * 			param)
{
	mui_stdfile_t * std = (mui_stdfile_t*)win;

	switch (what) {
		case MUI_WINDOW_ACTION_CLOSE: {
			// dispose of anything we had allocated
			printf("%s close\n", __func__);
			if (std->pref_file)
				free(std->pref_file);
			if (std->re_pattern)
				free(std->re_pattern);
			if (std->current_path)
				free(std->current_path);
			if (std->selected_path)
				free(std->selected_path);
			regfree(&std->re);
			for (int i = 0; i < (int)std->pop_path.count; i++)
				free(std->pop_path.e[i]);
			std->pop_path.count = 0;

		}	break;
	}
	return 0;
}

static int
_mui_stdfile_control_action(
		mui_control_t * c,
		void * 			cb_param,
		uint32_t 		what,
		void * 			param)
{
	mui_stdfile_t * std = cb_param;
	switch (c->uid) {
		case MUI_STD_FILE_PART_OK: {
			mui_listbox_elem_t * e = mui_listbox_get_elems(std->listbox)->e;
			int idx = mui_control_get_value(std->listbox);
			if (idx < 0 || idx >= (int)mui_listbox_get_elems(std->listbox)->count)
				return 0;
			mui_listbox_elem_t * elem = &e[idx];
			if (elem->disabled)
				break;
			// save pref file
			if (std->pref_file) {
				FILE * f = fopen(std->pref_file, "w");
				if (f) {
					fprintf(f, "%s\n", std->current_path);
					fclose(f);
				}
			}
			_mui_stdfile_control_action(std->listbox, std,
					MUI_CONTROL_ACTION_SELECT, elem);
		}	break;
		case MUI_STD_FILE_PART_CANCEL:
			mui_window_action(&std->win, MUI_STDF_ACTION_CANCEL, NULL);
			break;
		case MUI_STD_FILE_PART_HOME:
		//	printf("%s Home\n", __func__);
			_mui_stdfile_populate(std, getenv("HOME"));
			break;
		case MUI_STD_FILE_PART_ROOT:
		//	printf("%s Root\n", __func__);
			_mui_stdfile_populate(std, "/");
			break;
		case MUI_STD_FILE_PART_LISTBOX: {
		//	printf("%s Listbox\n", __func__);
			if (what == MUI_CONTROL_ACTION_SELECT ||
					what == MUI_CONTROL_ACTION_DOUBLECLICK) {
				mui_listbox_elem_t * e = param;
				if (e->disabled)
					break;
				char * full_path = NULL;
				asprintf(&full_path, "%s/%s",
						std->current_path, (char*)e->elem);
				char *dbl;
				while ((dbl = strstr(full_path, "//")) != NULL) {
					memmove(dbl, dbl + 1, strlen(dbl)); // include zero
				}
				struct stat st;
				stat(full_path, &st);
				if (S_ISDIR(st.st_mode)) {
					_mui_stdfile_populate(std, full_path);
				} else {
					printf("Selected: %s\n", full_path);
					mui_window_action(&std->win, MUI_STDF_ACTION_SELECT, NULL);
				}
				free(full_path);
			}
		}	break;
		case MUI_STD_FILE_PART_POPUP:
		//	printf("%s POPUP\n", __func__);
			if (what == MUI_CONTROL_ACTION_VALUE_CHANGED) {
				int idx = mui_control_get_value(c);
				printf("Selected: %s\n", std->pop_path.e[idx]);
				_mui_stdfile_populate(std, std->pop_path.e[idx]);
			}
			break;
	}
	return 0;
}

mui_window_t *
mui_stdfile_get(
		struct mui_t * ui,
		c2_pt_t where,
		const char * prompt,
		const char * regexp,
		const char * start_path )
{
	c2_rect_t wpos = C2_RECT_WH(where.x, where.y, 700, 400);
	if (where.x == 0 && where.y == 0)
		c2_rect_offset(&wpos,
			(ui->screen_size.x / 2) - (c2_rect_width(&wpos) / 2),
			(ui->screen_size.y * 0.4) - (c2_rect_height(&wpos) / 2));
	mui_window_t *w = mui_window_create(ui,
					wpos,
					NULL, MUI_WINDOW_LAYER_MODAL,
					prompt, sizeof(mui_stdfile_t));
	mui_window_set_action(w, _mui_stdfile_window_action, NULL);
	mui_stdfile_t *std = (mui_stdfile_t *)w;
	if (regexp) {
		std->re_pattern = strdup(regexp);
		int re = regcomp(&std->re, std->re_pattern, REG_EXTENDED);
		if (re) {
			char * msg = NULL;
			asprintf(&msg, "%s\n%s", std->re_pattern,
						strerror(errno));
			mui_alert(std->win.ui, C2_PT(0,0),
						"Could not compile regexp",
						msg,
						MUI_ALERT_FLAG_OK);
			free(std->re_pattern);
			std->re_pattern = NULL;
		}
	}
	mui_control_t * c = NULL;
	c2_rect_t cf;
	cf = C2_RECT_WH(0, 0, 120, 40);
	c2_rect_left_of(&cf, c2_rect_width(&w->content), 20);
	c2_rect_top_of(&cf, c2_rect_height(&w->content), 20);
	std->cancel = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Cancel", MUI_STD_FILE_PART_CANCEL);
	c2_rect_top_of(&cf, cf.t, 20);
	std->ok = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_DEFAULT,
					"Select", MUI_STD_FILE_PART_OK);

	std->ok->key_equ = MUI_KEY_EQU(0, 13); // return
	std->cancel->key_equ = MUI_KEY_EQU(0, 27); // ESC

	c2_rect_t t = cf;
	t.b = t.t + 1;
	c2_rect_top_of(&t, cf.t, 25);
	c = mui_separator_new(w, t);

	c2_rect_top_of(&cf, cf.t, 40);
	std->home = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Home", MUI_STD_FILE_PART_HOME);
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, 'h');

	c2_rect_top_of(&cf, cf.t, 20);
	std->root = c = mui_button_new(w,
					cf, MUI_BUTTON_STYLE_NORMAL,
					"Root", MUI_STD_FILE_PART_ROOT);
	c->key_equ = MUI_KEY_EQU(MUI_MODIFIER_ALT, '/');

	cf = C2_RECT_WH(15, 45, 700-185, 300);
	std->listbox = c = mui_listbox_new(w, cf,
					MUI_STD_FILE_PART_LISTBOX);

	cf = C2_RECT_WH(15, 0, 700-185, 34);
	c2_rect_top_of(&cf, std->listbox->frame.t, 6);
	std->popup = c = mui_popupmenu_new(w, cf,
					"Popup", MUI_STD_FILE_PART_POPUP);
//	printf("Popup: %p\n", c);
	c = NULL;
	TAILQ_FOREACH(c, &w->controls, self) {
		if (mui_control_get_uid(c) == 0)
			continue;
		mui_control_set_action(c, _mui_stdfile_control_action, std);
	}
	int dopop = 1; // populate to start_path by default
	if (ui->pref_directory) {
		uint32_t hash = std->re_pattern ? mui_hash(std->re_pattern) : 0;
		asprintf(&std->pref_file, "%s/std_path_%04x", ui->pref_directory, hash);
		printf("%s pref file: %s\n", __func__, std->pref_file);
		/* read last used pathname */
		FILE * f = fopen(std->pref_file, "r");
		if (f) {
			char * path = NULL;
			size_t len = 0;
			getline(&path, &len, f);
			fclose(f);
			if (path) {
				char *nl = strrchr(path, '\n');
				if (nl)
					*nl = 0;
				if (_mui_stdfile_populate(std, path) == 0) {
					printf("%s last path[%s]: %s\n", __func__,
						std->re_pattern, path);
					dopop = 0;
				}
				free(path);
			}
		}
	}
	if (dopop)
		_mui_stdfile_populate(std, start_path);

	return w;
}

char *
mui_stdfile_get_path(
		mui_window_t * w )
{
	mui_stdfile_t * std = (mui_stdfile_t *)w;
	return std->current_path;
}

char *
mui_stdfile_get_selected_path(
		mui_window_t * w )
{
	mui_stdfile_t * std = (mui_stdfile_t *)w;

	mui_listbox_elem_t * e = mui_listbox_get_elems(std->listbox)->e;
	int idx = mui_control_get_value(std->listbox);
	if (idx < 0 || idx >= (int)mui_listbox_get_elems(std->listbox)->count)
		return NULL;
	mui_listbox_elem_t * elem = &e[idx];
	char * full_path = NULL;
	asprintf(&full_path, "%s/%s",
			std->current_path, (char*)elem->elem);
	char *dbl;
	while ((dbl = strstr(full_path, "//")) != NULL) {
		memmove(dbl, dbl + 1, strlen(dbl)); // include zero
	}
	return full_path;
}
