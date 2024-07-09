/*
 * Copyright (C) 2024 Ev Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define EV_TYPE_FORMAT_BUILDER (ev_format_builder_get_type ())

G_DECLARE_FINAL_TYPE (EvFormatBuilder, ev_format_builder, EV, FORMAT_BUILDER, GObject)

EvFormatBuilder *ev_format_builder_new (void);
void             ev_format_builder_set_indent (EvFormatBuilder *self, int indent);
int              ev_format_builder_get_indent (EvFormatBuilder *self);
void             ev_format_builder_add (EvFormatBuilder *self, const char *key, const char *value);
void             ev_format_builder_take (EvFormatBuilder *self, char *key, char *value);
void             ev_format_builder_add_newline (EvFormatBuilder *self);
GString         *ev_format_builder_end (EvFormatBuilder *self);

G_END_DECLS
