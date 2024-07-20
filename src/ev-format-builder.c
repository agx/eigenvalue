/*
 * Copyright (C) 2024 Ev Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "ev-config.h"

#include "ev-format-builder.h"

/**
 * EvFormatBuilder:
 *
 * Output formatter for key value pairs. Formats the given
 * key / value pairs like aligned to `:`:
 *
 * ```
 *       a key : value1
 * another key : value2
 *        key3 : value3
 * ````
 */
struct _EvFormatBuilder {
  GObject    parent;

  int        indent;
  GPtrArray *keys;
  GPtrArray *values;
};
G_DEFINE_TYPE (EvFormatBuilder, ev_format_builder, G_TYPE_OBJECT)


static void
ev_format_builder_finalize (GObject *object)
{
  EvFormatBuilder *self = EV_FORMAT_BUILDER (object);

  g_clear_pointer (&self->keys, g_ptr_array_unref);
  g_clear_pointer (&self->values, g_ptr_array_unref);

  G_OBJECT_CLASS (ev_format_builder_parent_class)->finalize (object);
}


static void
ev_format_builder_class_init (EvFormatBuilderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = ev_format_builder_finalize;
}


static void
ev_format_builder_init (EvFormatBuilder *self)
{
  self->keys = g_ptr_array_new_full (10, g_free);
  self->values = g_ptr_array_new_full (10, g_free);
}


EvFormatBuilder *
ev_format_builder_new (void)
{
  return g_object_new (EV_TYPE_FORMAT_BUILDER, NULL);
}


void
ev_format_builder_set_indent (EvFormatBuilder *self, int indent)
{
  g_assert (EV_IS_FORMAT_BUILDER (self));

  self->indent = indent;
}


int
ev_format_builder_get_indent (EvFormatBuilder *self)
{
  g_assert (EV_IS_FORMAT_BUILDER (self));

  return self->indent;
}

/**
 * ev_format_builder_add:
 * @self: The builder
 * @key: The key to add
 * @value:(nullable): The value to added
 *
 * Adds a key value pair to the formater
 */
void
ev_format_builder_add (EvFormatBuilder *self, const char *key, const char *value)
{
  g_assert (EV_IS_FORMAT_BUILDER (self));
  g_assert (key);

  g_ptr_array_add (self->keys, g_strdup (key));
  g_ptr_array_add (self->values, g_strdup (value));
}

/**
 * ev_format_builder_add_nonnull:
 * @self: The builder
 * @key: The key to add
 * @value:(nullable): The value to added
 *
 * Adds a key value pair to the formater. If value is `NULL` nothing is
 * added;
 */
void
ev_format_builder_add_nonnull (EvFormatBuilder *self, const char *key, const char *value)
{
  g_assert (EV_IS_FORMAT_BUILDER (self));
  g_assert (key);

  if (!value)
    return;

  g_ptr_array_add (self->keys, g_strdup (key));
  g_ptr_array_add (self->values, g_strdup (value));
}

/**
 * ev_format_builder_take:
 * @self: The builder
 * @key: The key to add
 * @value:(nullable): The value to added
 *
 * Like [method@FormatBuilder.add] but takes ownership of `key`
 * and `value`.
 */
void
ev_format_builder_take (EvFormatBuilder *self, char *key, char *value)
{
  g_assert (EV_IS_FORMAT_BUILDER (self));
  g_assert (key);

  g_ptr_array_add (self->keys, key);
  g_ptr_array_add (self->values, value);
}


void
ev_format_builder_add_newline (EvFormatBuilder *self)
{
  g_assert (EV_IS_FORMAT_BUILDER (self));

  g_ptr_array_add (self->keys, NULL);
  g_ptr_array_add (self->values, NULL);
}


GString *
ev_format_builder_end (EvFormatBuilder *self)
{
  int max_len = 0;
  GString *out = g_string_new ("");

  g_assert (EV_IS_FORMAT_BUILDER (self));
  g_assert (self->keys->len == self->values->len);

  for (int i = 0; i < self->keys->len; i++) {
    char *key = g_ptr_array_index (self->keys, i);

    if (!key)
      continue;

    if (strlen (key) > max_len)
      max_len = strlen (key);
  }

  max_len += self->indent;

  for (int i = 0; i < self->keys->len; i++) {
    char *key = g_ptr_array_index (self->keys, i);
    char *value = g_ptr_array_index (self->values, i);

    if (!key) {
      g_string_append (out, "\n");
      continue;
    }

    value = value ?: "";
    g_string_append_printf (out, "%*s : %s\n", max_len, key, value);
  }

  return out;
}
