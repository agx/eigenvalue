/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "ev-application.h"

#include <glib.h>

G_BEGIN_DECLS

#define INFO_INDENT 4

/**
 * EvCmdOptFlagOptional:
 *
 * @EV_CMD_OPT_FLAG_OPTIONAL: The argument is optional
 */
typedef enum {
  EV_CMD_OPT_FLAG_NONE     = 0,
  EV_CMD_OPT_FLAG_OPTIONAL = (1 << 0),
} EvCmdOptFlags;

typedef GStrv EvCmdOptCompl (const char *word, int len);

/**
 * EvCmdOpt:
 *
 * Describes an option argument of a command
 */
typedef struct _EvCmdOpt {
  const char          *name;
  const char          *desc;
  const EvCmdOptFlags  flags;
  EvCmdOptCompl       *completer;
} EvCmdOpt;

/**
 * EvCmdFunc:
 * @args: THe arguments
 * @error: Location for a recoverable error
 *
 * The function run to execute the command.
 */
typedef GString *EvCmdFunc (GStrv args, GError **error);

/**
 * EvCmd:
 *
 * A '/' command in the prompt
 */
typedef struct _EvCmd {
  char            *name;
  char            *help_summary;
  EvCmdFunc       *func;
  const EvCmdOpt  *opts;
} EvCmd;

void ev_prompt_init         (GPtrArray *commands, const char *cache_dir);
void ev_prompt_destroy      (const char *cache_dir);
void ev_prompt_add_commands (GPtrArray *commands);

G_END_DECLS
