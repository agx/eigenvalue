/*
 * Copyright (C) 2024 Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "ev-config.h"

#include "ev-application.h"
#include "ev-prompt.h"
#include "ev-matrix.h"

#define BLURP "A matrix client for the terminal"


/**
 * EvApplication:
 *
 * The application object
 */


typedef enum _EvDebugFlags {
  EV_DEBUG_FLAG_NONE      = 0,
  EV_DEBUG_FLAG_NO_MATRIX = 1 << 0,
} EvDebugFlags;


struct _EvApplication {
  GApplication   parent;

  char          *data_dir;
  char          *cache_dir;
  EvDebugFlags   debug_flags;
};
G_DEFINE_TYPE (EvApplication, ev_application, G_TYPE_APPLICATION)


static GDebugKey debug_keys[] =
{
 { .key = "no-matrix",
   .value = EV_DEBUG_FLAG_NO_MATRIX,
 },
};


static void
ev_application_activate (GApplication *app)
{
  g_application_hold (app);
}


static void
ev_application_startup (GApplication *app)
{
  EvApplication *self = EV_APPLICATION (app);
  g_autoptr (GPtrArray) commands = g_ptr_array_new ();

  if ((self->debug_flags & EV_DEBUG_FLAG_NO_MATRIX) == 0) {
    ev_matrix_init (self->data_dir, self->cache_dir);
    ev_matrix_add_commands (commands);
  }

  ev_prompt_add_commands (commands);
  ev_prompt_init (commands, self->cache_dir);

  G_APPLICATION_CLASS (ev_application_parent_class)->startup (app);
}


static void
ev_application_shutdown (GApplication *app)
{
  ev_prompt_destroy (EV_APPLICATION (app)->cache_dir);
  ev_matrix_destroy ();

  G_APPLICATION_CLASS (ev_application_parent_class)->shutdown (app);
}


G_NORETURN static void
print_version (void)
{
  g_print ("%s %s - " BLURP "\n", EV_PROJECT, EV_VERSION);
  exit (0);
}


static int
ev_application_handle_local_options (GApplication *app, GVariantDict *options)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (ev_application_parent_class);

  if (g_variant_dict_contains (options, "version")) {
    print_version ();

    return 0;
  }

  return app_class->handle_local_options (app, options);
}


static void
ev_application_finalize (GObject *object)
{
  EvApplication *self = EV_APPLICATION (object);

  g_free (self->cache_dir);
  g_free (self->data_dir);

  G_OBJECT_CLASS (ev_application_parent_class)->finalize (object);
}


static void
ev_application_class_init (EvApplicationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  object_class->finalize = ev_application_finalize;

  app_class->activate = ev_application_activate;
  app_class->startup = ev_application_startup;
  app_class->shutdown = ev_application_shutdown;
  app_class->handle_local_options = ev_application_handle_local_options;
}


static void
ev_application_init (EvApplication *self)
{
  const char *debugenv;

  self->data_dir = g_build_filename (g_get_user_data_dir (), EV_PROJECT, NULL);
  self->cache_dir = g_build_filename (g_get_user_cache_dir (), EV_PROJECT, NULL);

  g_application_set_option_context_parameter_string (G_APPLICATION (self), BLURP);
  g_application_set_version (G_APPLICATION (self), EV_VERSION);

  debugenv = g_getenv ("EV_DEBUG");
  if (debugenv)
    self->debug_flags = g_parse_debug_string (debugenv, debug_keys, G_N_ELEMENTS (debug_keys));
}


EvApplication *
ev_application_new (void)
{
  return g_object_new (EV_TYPE_APPLICATION, NULL);
}


void
ev_quit (void)
{
  g_application_release (g_application_get_default ());
}


const char *
ev_application_get_cache_dir (EvApplication *self)
{
  g_assert (EV_IS_APPLICATION (self));

  return self->cache_dir;
}


const char *
ev_application_get_data_dir (EvApplication *self)
{
  g_assert (EV_IS_APPLICATION (self));

  return self->data_dir;
}
