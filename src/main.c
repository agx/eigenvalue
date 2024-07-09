/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "ev-config.h"
#include "ev-application.h"

#include <glib/gi18n.h>
#include <glib-unix.h>
#include <stdio.h>

#include "cmatrix.h"


static gboolean
on_signal (gpointer unused)
{
  ev_quit ();
  return G_SOURCE_REMOVE;
}


int
main (int argc, char *argv[])
{
  g_autoptr (EvApplication) app = NULL;
  int ret;

  cm_init (TRUE);

  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);

  g_unix_signal_add (SIGTERM, on_signal, NULL);
  g_unix_signal_add (SIGINT, on_signal, NULL);

  app = ev_application_new ();
  ret = g_application_run (G_APPLICATION (app), argc, argv);

  return ret;
}
