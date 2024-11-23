/*
 * mui_mui_settings.c
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

#include "mui.h"
#include "mii_mui_settings.h"

IMPLEMENT_C_ARRAY(mii_config_array);

mii_config_line_t *
mii_config_get_section(
	mii_config_file_t *		cf,
	const char * 			section,
	bool 					add )
{
	mii_config_line_t * cl;
	for (int i = 0; i < (int)cf->line.count; i++) {
		cl = cf->line.e[i];
		if (cl->section && !strcmp(cl->key, section))
			return cl;
	}
	if (!add)
		return NULL;
	cl = calloc(1, sizeof(*cl) + strlen(section) + 3);
	if (!cl)
		return NULL;
	cl->section = 1;
	sprintf(cl->line, "[%s", section);
	cl->key = cl->line + 1;
	cl->number = cf->line.count;
	mii_config_array_push(&cf->line, cl);
	return cl;
}

mii_config_line_t *
mii_config_get(
	mii_config_file_t *		cf,
	mii_config_line_t *		section,
	const char * 			key)
{
	if (!cf || !section || !key)
		return NULL;
	for (int i = section->number + 1; i < (int)cf->line.count; i++) {
		mii_config_line_t * cl = cf->line.e[i];
		if (cl->section)
			return NULL;
		if (!strcmp(cl->key, key))
			return cl;
	}
	return NULL;
}

mii_config_line_t *
mii_config_set(
	mii_config_file_t *		cf,
	mii_config_line_t *		section,
	const char * 			key,
	const char * 			value)
{
	if (!cf || !section || !key)
		return NULL;
	mii_config_line_t * cl = mii_config_get(cf, section, key);

	int idx = section->number + 1;
	if (cl) {
		if (value && cl->value && !strcmp(cl->value, value))
			return cl;
		idx = cl->number;
		mii_config_array_delete(&cf->line, idx, 1);
		free(cl);
	}
	cl = calloc(1, sizeof(*cl) + strlen(key) + strlen(value) + 3);
	strcpy(cl->line, key);
	// this wouldnt work if memory was not zeroes by calloc
	strcpy(cl->line + strlen(key) + 1, value ? value : "");
	cl->key = cl->line;
	cl->value = cl->line + strlen(key) + 1;
	mii_config_array_insert(&cf->line, idx, &cl, 1);
	for (int i = idx; i < (int)cf->line.count; i++)
		cf->line.e[i]->number = i;

	return cl;
}

int
mii_config_file_load(
	mii_config_file_t *		cf,
	const char * 			path )
{
	int res = -1;
	mii_config_array_clear(&cf->line);
	FILE * f = fopen(path, "r");
	if (!f)
		return -1;
	cf->path = strdup(path);
	char line[512];
	int n = 0;
	while (fgets(line, sizeof(line), f)) {
		int l = strlen(line);
		while (l && (line[l-1] == '\n' || line[l-1] == ' ' || line[l-1] == '\t'))
			line[--l] = 0;
		mii_config_line_t * cl = calloc(1, sizeof(*cl) + l + 1);
		if (!cl)
			goto exit;
		cl->number = n++;
		strcpy(cl->line, line);
		mii_config_array_push(&cf->line, cl);

		char * s = cl->line;
		while (isspace(*s))
			s++;
		if (*s == '#' || *s == ';' || *s == 0)
			cl->ignore = 1;	// comment or empty line
		else if (*s == '[') {
			char * d = s + 1;
			char * e = strchr(d, ']');
			if (e)
				*e = 0;
			cl->key = d;
			cl->value = NULL;
			cl->section = 1;
		} else {
			char * d = s;
			char * k = strsep(&d, " =");
			cl->key = k;
			// we want a pointer to zero here if there's no value
			cl->value = d ? d : s + strlen(s);
		}
	}
	res = 0;
exit:
	fclose(f);
	return res;
}

// same as previous function, but write the file back
int
mii_settings_save(
	mii_config_file_t *		cf)
{
	if (!cf || !cf->path)
		return -1;
	FILE * f = fopen(cf->path, "w");
	if (!f)
		return -1;
	mii_config_line_t * cl;
	for (int i = 0; i < (int)cf->line.count; i++) {
		cl = cf->line.e[i];
		if (cl->section)
			fprintf(f, "[%s]\n", cl->key);
		else if (cl->ignore)
			fprintf(f, "%s\n", cl->line);
		else
			fprintf(f, "%s=%s\n", cl->key, cl->value ? cl->value : "");
	}
	fclose(f);
	return 0;
}

int
mii_settings_load(
	mii_config_file_t *		cf,
	const char * 			path,
	const char * 			file )
{
	char * full_path = NULL;
	if (path) {
		asprintf(&full_path, "%s/%s", path, file);
	} else {
		full_path = strdup(file);
	}
	if (!full_path)
		return -1;

	int r = mii_config_file_load(cf, full_path);
	free(full_path);
	return r;
}

const mii_slot_driver_t mii_slot_driver[MII_SLOT_DRIVER_COUNT] = {
	[MII_SLOT_DRIVER_NONE] 		= {
		.driver = "none", .label = "", .description = ""},
	[MII_SLOT_DRIVER_SMARTPORT]	= {
		.driver = "smartport", .label = "", .description = ""},
	[MII_SLOT_DRIVER_DISK2] 	= {
		.driver = "disk2", .label = "", .description = ""},
	[MII_SLOT_DRIVER_MOUSE] 	= {
		.driver = "mouse", .label = "", .description = ""},
	[MII_SLOT_DRIVER_SSC] 		= {
		.driver = "ssc", .label = "", .description = ""},
	[MII_SLOT_DRIVER_ROM1MB]	= {
		.driver = "eecard", .label = "", .description = ""},
	[MII_SLOT_DRIVER_MOCKINGBOARD] = {
		.driver = "mockingboard", .label = "", .description = ""},
#ifdef MII_DANII
	[MII_SLOT_DRIVER_DANII]		= {
		.driver = "danii", .label = "", .description = ""},
#endif
};

int
mii_emu_save(
	mii_config_file_t *		cf,
	mii_machine_config_t *	config )
{
	if (!cf || !config)
		return -1;
	char label[64];
	mii_config_line_t * section = mii_config_get_section(cf, "emu", true);

	mii_config_set(cf, section, "titan",
				config->titan_accelerator ? "1" : "0");
	mii_config_set(cf, section, "no_slot_clock",
				config->no_slot_clock ? "1" : "0");
	mii_config_set(cf, section, "audio_muted",
				config->audio_muted ? "1" : "0");
	// audio volume
	sprintf(label, "%.2f", config->audio_volume);
	mii_config_set(cf, section, "audio_volume", label);
	sprintf(label, "%d", config->video_mode);
	mii_config_set(cf, section, "video_mode", label);

	section = mii_config_get_section(cf, "joystick", true);
	mii_config_set(cf, section, "device",
				config->joystick.device);
	for (int i = 0; i < 2; i++) {
		sprintf(label, "%d", config->joystick.buttons[i]);
		char name[32] = "button0";
		name[6] += i;
		mii_config_set(cf, section, name, label);
		strcpy(name, "axis0");
		name[4] += i;
		sprintf(label, "%d", config->joystick.axes[i]);
		mii_config_set(cf, section, name, label);
	}
	section = mii_config_get_section(cf, "loadbin", true);
	mii_config_set(cf, section, "path",
				config->loadbin.path);
	sprintf(label, "%d", config->loadbin.active);
	mii_config_set(cf, section, "active", label);
	sprintf(label, "%d", config->loadbin.bank);
	mii_config_set(cf, section, "bank", label);
	sprintf(label, "%d", config->loadbin.addr);
	mii_config_set(cf, section, "addr", label);
	sprintf(label, "%d", config->loadbin.auto_reload);
	mii_config_set(cf, section, "auto_reload", label);

	for (int i = 0; i < 7; i++) {
		char key[32];
		sprintf(key, "slot_%d", i+1);
		section = mii_config_get_section(cf, key, true);

		switch(config->slot[i].driver) {
			case MII_SLOT_DRIVER_SMARTPORT:
				mii_config_set(cf, section, "image0",
					config->slot[i].conf.smartport.drive[0].disk);
				sprintf(label, "%lu", config->slot[i].conf.smartport.drive[0].flags);
				mii_config_set(cf, section, "flags0", label);
				mii_config_set(cf, section, "image1",
					config->slot[i].conf.smartport.drive[1].disk);
				sprintf(label, "%lu", config->slot[i].conf.smartport.drive[1].flags);
				mii_config_set(cf, section, "flags1", label);
				break;
			case MII_SLOT_DRIVER_DISK2:
				mii_config_set(cf, section, "image0",
					config->slot[i].conf.disk2.drive[0].disk);
				sprintf(label, "%lu", config->slot[i].conf.disk2.drive[0].flags);
				mii_config_set(cf, section, "flags0", label);
				mii_config_set(cf, section, "wp0",
					config->slot[i].conf.disk2.drive[0].wp ? "1" : "0");
				mii_config_set(cf, section, "image1",
					config->slot[i].conf.disk2.drive[1].disk);
				sprintf(label, "%lu", config->slot[i].conf.disk2.drive[1].flags);
				mii_config_set(cf, section, "flags1", label);
				mii_config_set(cf, section, "wp1",
					config->slot[i].conf.disk2.drive[1].wp ? "1" : "0");
				break;
			case MII_SLOT_DRIVER_SSC:
				sprintf(label, "%u", config->slot[i].conf.ssc.kind);
				mii_config_set(cf, section, "kind", label);
				mii_config_set(cf, section, "device",
					config->slot[i].conf.ssc.device);
				sprintf(label, "%u", config->slot[i].conf.ssc.socket_port);
				mii_config_set(cf, section, "port", label);
				sprintf(label, "%u", config->slot[i].conf.ssc.baud);
				mii_config_set(cf, section, "baud", label);
				sprintf(label, "%u", config->slot[i].conf.ssc.bits);
				mii_config_set(cf, section, "bits", label);
				sprintf(label, "%u", config->slot[i].conf.ssc.parity);
				mii_config_set(cf, section, "parity", label);
				sprintf(label, "%u", config->slot[i].conf.ssc.stop);
				mii_config_set(cf, section, "stop", label);
				sprintf(label, "%u", config->slot[i].conf.ssc.hw_handshake);
				mii_config_set(cf, section, "hw_handshake", label);
				break;
			case MII_SLOT_DRIVER_ROM1MB:
				mii_config_set(cf, section, "use_default",
					config->slot[i].conf.rom1mb.use_default ? "1" : "0");
				mii_config_set(cf, section, "image",
					config->slot[i].conf.rom1mb.drive.disk);
				break;
		}
		mii_config_set(cf, section, "driver",
				mii_slot_driver[config->slot[i].driver].driver);
	}
	mii_settings_save(cf);
	return 0;
}

int
mii_emu_load(
	mii_config_file_t *		cf,
	mii_machine_config_t *	config )
{
	if (!cf || !config)
		return -1;
	mii_config_line_t * section = mii_config_get_section(cf, "emu", false);
	if (section) {
		mii_config_line_t * cl = mii_config_get(cf, section, "titan");
		if (cl)
			config->titan_accelerator = atoi(cl->value);
		cl = mii_config_get(cf, section, "no_slot_clock");
		if (cl)
			config->no_slot_clock = atoi(cl->value);
		cl = mii_config_get(cf, section, "audio_muted");
		if (cl)
			config->audio_muted = !!atoi(cl->value);
		cl = mii_config_get(cf, section, "audio_volume");
		if (cl)
			config->audio_volume = atof(cl->value);
		cl = mii_config_get(cf, section, "video_mode");
		if (cl)
			config->video_mode = atoi(cl->value);
	}

	section = mii_config_get_section(cf, "joystick", false);
	if (section) {
		mii_config_line_t * cl = mii_config_get(cf, section, "device");
		if (cl)
			strcpy(config->joystick.device, cl->value);
		for (int i = 0; i < 2; i++) {
			char name[32] = "button0";
			name[6] += i;
			cl = mii_config_get(cf, section, name);
			if (cl)
				config->joystick.buttons[i] = atoi(cl->value);
			strcpy(name, "axis0");
			name[4] += i;
			cl = mii_config_get(cf, section, name);
			if (cl)
				config->joystick.axes[i] = atoi(cl->value);
		}
	}
	section = mii_config_get_section(cf, "loadbin", false);
	if (section) {
		mii_config_line_t * cl = mii_config_get(cf, section, "path");
		if (cl)
			strcpy(config->loadbin.path, cl->value);
		cl = mii_config_get(cf, section, "active");
		if (cl)
			config->loadbin.active = atoi(cl->value);
		cl = mii_config_get(cf, section, "bank");
		if (cl)
			config->loadbin.bank = atoi(cl->value);
		cl = mii_config_get(cf, section, "addr");
		if (cl)
			config->loadbin.addr = atoi(cl->value);
		cl = mii_config_get(cf, section, "auto_reload");
		if (cl)
			config->loadbin.auto_reload = atoi(cl->value);
	}

	for (int i = 0; i < 7; i++) {
		char key[32];
		sprintf(key, "slot_%d", i+1);
		section = mii_config_get_section(cf, key, false);
		if (!section)
			continue;

		mii_config_line_t * cl = mii_config_get(cf, section, "driver");
		if (!cl)
			continue;
		for (int j = 0; j < MII_SLOT_DRIVER_COUNT; j++) {
			if (!strcmp(mii_slot_driver[j].driver, cl->value)) {
				config->slot[i].driver = j;
				break;
			}
		}
		switch (config->slot[i].driver) {
			case MII_SLOT_DRIVER_SMARTPORT:
				cl = mii_config_get(cf, section, "image0");
				if (cl)
					strcpy(config->slot[i].conf.smartport.drive[0].disk, cl->value);
				cl = mii_config_get(cf, section, "flags0");
				if (cl)
					config->slot[i].conf.smartport.drive[0].flags = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "image1");
				if (cl)
					strcpy(config->slot[i].conf.smartport.drive[1].disk, cl->value);
				cl = mii_config_get(cf, section, "flags1");
				if (cl)
					config->slot[i].conf.smartport.drive[1].flags = strtoul(cl->value, NULL, 0);
				break;
			case MII_SLOT_DRIVER_DISK2:
				cl = mii_config_get(cf, section, "image0");
				if (cl)
					strcpy(config->slot[i].conf.disk2.drive[0].disk, cl->value);
				cl = mii_config_get(cf, section, "flags0");
				if (cl)
					config->slot[i].conf.disk2.drive[0].flags = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "wp0");
				if (cl)
					config->slot[i].conf.disk2.drive[0].wp = !!strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "image1");
				if (cl)
					strcpy(config->slot[i].conf.disk2.drive[1].disk, cl->value);
				cl = mii_config_get(cf, section, "flags1");
				if (cl)
					config->slot[i].conf.disk2.drive[1].flags = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "wp1");
				if (cl)
					config->slot[i].conf.disk2.drive[1].wp = !!strtoul(cl->value, NULL, 0);
				break;
			case MII_SLOT_DRIVER_SSC:
				cl = mii_config_get(cf, section, "kind");
				if (cl)
					config->slot[i].conf.ssc.kind = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "device");
				if (cl)
					strcpy(config->slot[i].conf.ssc.device, cl->value);
				cl = mii_config_get(cf, section, "port");
				if (cl)
					config->slot[i].conf.ssc.socket_port = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "baud");
				if (cl)
					config->slot[i].conf.ssc.baud = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "bits");
				if (cl)
					config->slot[i].conf.ssc.bits = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "parity");
				if (cl)
					config->slot[i].conf.ssc.parity = strtoul(cl->value, NULL, 0);
				cl = mii_config_get(cf, section, "stop");
				if (cl)
					config->slot[i].conf.ssc.stop = strtoul(cl->value, NULL, 0);
				break;
			case MII_SLOT_DRIVER_ROM1MB:
				cl = mii_config_get(cf, section, "use_default");
				if (cl)
					config->slot[i].conf.rom1mb.use_default = atoi(cl->value);
				cl = mii_config_get(cf, section, "image");
				if (cl)
					strcpy(config->slot[i].conf.rom1mb.drive.disk, cl->value);
				break;
		}
	}
	return 0;
}