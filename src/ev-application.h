/*
 * Copyright (C) 2024 Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define EV_TYPE_APPLICATION (ev_application_get_type ())

G_DECLARE_FINAL_TYPE (EvApplication, ev_application, EV, APPLICATION, GApplication)

EvApplication    *ev_application_new (void);
const char       *ev_application_get_data_dir (EvApplication *self);
const char       *ev_application_get_cache_dir (EvApplication *self);

void ev_quit (void);


G_END_DECLS
