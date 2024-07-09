/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "ev-config.h"
#include "ev-format-builder.h"
#include "ev-matrix.h"
#include "ev-prompt.h"

#include <glib-unix.h>
#include <glib/gi18n.h>
#include <gio/gio.h>
#include <gio/gunixinputstream.h>
#include <histedit.h>
#include <readline.h>

#undef DEBUG_COMPLETION

/**
 * EvPrompt:
 *
 * Prompting, history, completion
 */

static GInputStream *stream;
static gint stdin_id;
static EditLine *el;
static History *hist;
static Tokenizer *tok;
static GPtrArray *commands;


static EvCmd *
ev_cmd (int n)
{
  g_assert (n < commands->len);

  return g_ptr_array_index (commands, n);
}


static const EvCmd *
ev_cmds_get (const char *command)
{
  if (!command)
    return NULL;

  for (int i = 0; i < commands->len; i++) {

    if (g_str_equal (command, ev_cmd (i)->name))
      return ev_cmd (i);
  }

  return NULL;
}


static const EvCmdOpt *
ev_cmd_get_opt (const char *command, guint num)
{
  const EvCmd *cmd = ev_cmds_get (command);

  if (!cmd)
    return NULL;

  if (cmd->opts == NULL)
    return NULL;

  if (!command)
    return NULL;

  for (int i = 0; cmd->opts[i].name; i++) {
    if (i == num)
      return &cmd->opts[i];
  }

  return NULL;
}


static GString *
ev_prompt_print_history (GStrv args, GError **err)
{
  int rv;
  HistEvent ev;
  g_autoptr (GString) out = g_string_new ("");

  for (rv = history (hist, &ev, H_LAST); rv != -1; rv = history (hist, &ev, H_PREV))
    g_string_append_printf (out, "%4d %s", ev.num, ev.str);

  return g_steal_pointer (&out);
}


static GString *
ev_prompt_quit (GStrv args, GError **err)
{
  ev_quit ();
  return g_string_new ("");
}


static GStrv
help_command_opt_get_completion (const char *word, int pos)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  for (int i = 0; i < commands->len; i++) {
    if (strncmp (ev_cmd (i)->name, word, pos) == 0)
      g_strv_builder_add (builder, ev_cmd (i)->name);
  }

  return g_strv_builder_end (builder);
}


static GString *
ev_prompt_print_help (GStrv args, GError **err)
{
  g_autoptr (EvFormatBuilder) builder = ev_format_builder_new ();

  if (args && g_strv_length (args) > 0) {
    GString *out = g_string_new ("");
    const char *name = args[0];
    const EvCmd *cmd = ev_cmds_get (name);

    if (!cmd) {
      g_string_append_printf (out, "Unknown command %s\n", name);
      return out;
    }

    g_string_append_printf (out, "  %s - %s\n\n", name, cmd->help_summary);
    g_string_append_printf (out, "  Usage:\n");
    g_string_append_printf (out, "    /%s", name);

    if (cmd->opts) {
      int max_opt_len = 0;

      for (int i = 0; cmd->opts[i].name; i++) {
        g_string_append_printf (out, " ");
        if (cmd->opts[i].flags & EV_CMD_OPT_FLAG_OPTIONAL)
          g_string_append_printf (out, "[");
        g_string_append_printf (out, "%s", cmd->opts[i].name);
        if (cmd->opts[i].flags & EV_CMD_OPT_FLAG_OPTIONAL)
          g_string_append_printf (out, "]");
      }

      g_string_append_printf (out, "\n");
      for (int i = 0; cmd->opts[i].name; i++) {
        if (strlen (cmd->opts[i].name) > max_opt_len)
          max_opt_len = strlen (cmd->opts[i].name);

      }

      max_opt_len += 4;
      for (int i = 0; cmd->opts[i].name; i++) {
        g_string_append_printf (out, "%*s : %s\n", max_opt_len,
                                cmd->opts[i].name,
                                cmd->opts[i].desc);
      }
    }

    g_string_append_printf (out, "\n");
    return out;
  }

  ev_format_builder_set_indent (builder, INFO_INDENT);
  for (int i = 0; i < commands->len; i++)
    ev_format_builder_add (builder,  ev_cmd (i)->name, ev_cmd (i)->help_summary);

  return ev_format_builder_end (builder);
}


static GStrv
complete_command (const char *word, int pos)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  if (word[0] != '/')
    return NULL;

  word++; pos--; /* skip '/' */
  for (int i = 0; i < commands->len; i++) {
    const char *name = ev_cmd (i)->name;

    if (pos > strlen (name))
      continue;

    if (strncmp (name, word, pos) == 0)
      g_strv_builder_take (builder, g_strdup_printf ("/%s", name));
  }

  return g_strv_builder_end (builder);
}


static unsigned char
print_or_insert_completions (GStrv completions, int pos)
{
  if (completions && g_strv_length (completions)) {
    /* More than one completion, display possible matches */
    if (g_strv_length (completions) > 1) {
      g_print ("\n");
      for (int i = 0; completions[i]; i++)
        g_print ("%s ", completions[i]);
      g_print ("\n");
      return CC_REDISPLAY;
    } else { /* One match, insert completion */
      if (el_insertstr (el, &completions[0][pos]) == -1) {
        return CC_ERROR;
      } else {
        el_insertstr (el, " ");
        return CC_REFRESH;
      }
    }
  }

  return CC_ERROR;
}


static unsigned char
complete (EditLine *cel, int ch)
{
  g_auto (GStrv) (completions) = NULL;
  int ac = 0, cc = 0, co = 0;
  unsigned char ret = CC_ERROR;
  const LineInfo *li = el_line (el);
  const char **av;
  const char *name, *match;
  const EvCmdOpt *opt;

  if (tok_line (tok, li, &ac, &av, &cc, &co) < 0) {
    g_critical ("Internal error parsing input");
    return CC_ERROR;
  }

#ifdef DEBUG_COMPLETION
  g_message ("DEBUG: %s: %d: ac: %d, cc %d, co %d", __func__, __LINE__, ac, cc, co);
#endif

  if (ac < 2 && cc == 0) {
    match = cc >= ac ? "" : av[cc];
    completions = complete_command (match, co);
    ret = print_or_insert_completions (completions, co);
    goto out;
  }

  /* Should have at least one arg as we did command completion above */
  if (ac == 0 || cc == 0)
    goto out;

  name = av[0];
  name++; /* skip '/' */

  opt = ev_cmd_get_opt (name, cc - 1);
  if (!opt)
    goto out;

  if (!opt->completer)
    goto out;

  match = cc >= ac ? "" : av[cc];
  completions = (opt->completer) (match, co);
  ret = print_or_insert_completions (completions, co);

 out:
  tok_reset (tok);
  return ret;
}


static const char *
prompt (void)
{
  return "Ev> ";
}


static void
reset (void)
{
  g_clear_pointer (&el, el_end);
  el = el_init (EV_PROJECT, stdin, stdout, stderr);
  el_set (el, EL_PROMPT, prompt);
  el_set (el, EL_EDITOR, "emacs");
  el_set (el, EL_UNBUFFERED, TRUE);
  el_set (el, EL_HIST, history, hist);

  el_set (el, EL_ADDFN, "ev-complete", "Context sensitive argument completion", complete);
  el_set (el, EL_BIND, "^I", "ev-complete", NULL);

  tok_reset (tok);
}


static void
run_command (const char *av[], int ac)
{
  g_autoptr (GString) out = NULL;
  g_autoptr (GError) err = NULL;
  gboolean found = FALSE;

  for (int i = 0; i < commands->len; i++) {
    if (g_strcmp0 (av[0] + 1, ev_cmd (i)->name) == 0) {
      g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
      g_auto (GStrv) args = NULL;

      for (int k = 1; k < ac; k++)
        g_strv_builder_add (builder, av[k]);

      args = g_strv_builder_end (builder);
      out = (ev_cmd (i)->func)(args, &err);

      if (out) {
        if (out->len)
          g_print ("\n%s\n", out->str);
      } else {
        g_print ("\033[31m");
        if (err)
          g_print ("Command failed: %s\n", err->message);
        else
          g_print ("Internal error - Command failed to set error\n");
        g_print ("\033[39m");
      }

      found = TRUE;
      break;
    }
  }
  if (!found)
    g_print ("\nUnknown command '%s'\n", av[0] + 1);
}


static gboolean
on_stdin_ready (int fd, GIOCondition condition, gpointer data)
{
  int ac = 0, cc = 0, co = 0;
  const LineInfo *li;
  const char **av, *buf;
  HistEvent ev;
  int num;

  buf = el_gets (el, &num);
  li = el_line (el);
  if (*(li->lastchar - 1) != '\n')
    return G_SOURCE_CONTINUE;

  if (tok_line (tok, li, &ac, &av, &cc, &co) < 0) {
    g_critical ("Internal error parsing input");
    goto done;
  }

  if (strlen (buf) == 0)
    goto done;

  if (strlen (buf) > 1)
    history (hist, &ev, H_ENTER, buf);

  if (av[0] && av[0][0] == '/')
    run_command (av, ac);

 done:
  /* FIXME: Is there a simpler way then resetting the whole state? */
  reset ();

  return G_SOURCE_CONTINUE;
}


void
ev_prompt_init (GPtrArray *commands_, const char *cache_dir)
{
  HistEvent ev;
  g_autofree char *hist_path = NULL;

  g_assert (!commands);
  commands = g_ptr_array_ref (commands_);

  hist = history_init ();
  history (hist, &ev, H_SETSIZE, 100);
  history (hist, &ev, H_SETUNIQUE, 1);

  hist_path = g_build_filename (cache_dir, "history", NULL);
  if (g_file_test (hist_path, G_FILE_TEST_EXISTS))
    history (hist, &ev, H_LOAD, hist_path);

  tok  = tok_init (NULL);
  reset ();

  stream = g_unix_input_stream_new (STDIN_FILENO, FALSE);
  stdin_id = g_unix_fd_add (g_unix_input_stream_get_fd (G_UNIX_INPUT_STREAM (stream)),
                            G_IO_IN, on_stdin_ready, NULL);
}


void
ev_prompt_destroy (const char *cache_dir)
{
  HistEvent ev;
  g_autofree char *hist_path = NULL;

  if (!g_file_test (cache_dir, G_FILE_TEST_EXISTS))
    g_mkdir (cache_dir, 0700);

  hist_path = g_build_filename (cache_dir, "history", NULL);
  history (hist, &ev, H_SAVE, hist_path);

  g_clear_pointer (&commands, g_ptr_array_unref);
  g_clear_pointer (&el, el_end);
  g_clear_pointer (&tok, tok_end);
  g_clear_pointer (&hist, history_end);

  g_clear_handle_id (&stdin_id, g_source_remove);
  g_clear_object (&stream);
}


static const EvCmdOpt help_opts[] = {
  {
    .name = "command",
    .desc = "The command to print help for",
    .flags = EV_CMD_OPT_FLAG_OPTIONAL,
    .completer = help_command_opt_get_completion,
  },
  /* Sentinel */
  { NULL }
};


static EvCmd prompt_commands[] = {
  /* Navigation */
  {
    .name = "help",
    .help_summary = N_("Show this help"),
    .func = ev_prompt_print_help,
    .opts = help_opts,
  },
  {
    .name = "history",
    .help_summary = N_("Print command history"),
    .func = ev_prompt_print_history,
  },
  {
    .name = "quit",
    .help_summary = N_("Quit the application"),
    .func = ev_prompt_quit,
  },
  /* Sentinel */
  { NULL }
};


void
ev_prompt_add_commands (GPtrArray *commands_)
{
  for (int i = 0; prompt_commands[i].name; i++)
    g_ptr_array_add (commands_, &prompt_commands[i]);
}
