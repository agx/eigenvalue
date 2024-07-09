/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ev-prompt.h"

#include <glib.h>

G_BEGIN_DECLS

void         ev_matrix_init         (const char *data_dir, const char *cache_dir);
void         ev_matrix_destroy      (void);
void         ev_matrix_add_commands (GPtrArray *commands);

G_END_DECLS
