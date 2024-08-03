/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Guido Günther <agx@sigxcpu.org>
 */

#include "ev-config.h"
#include "ev-application.h"
#include "ev-format-builder.h"
#include "ev-matrix.h"
#include "ev-prompt.h"

#include <gio/gio.h>
#include <glib/gi18n.h>

#include "cmatrix.h"

/**
 * EvMatrix:
 *
 * Matrix server interaction
 */

static CmMatrix *matrix;
static CmClient *client;
static CmAccount *account;
static GCancellable *cancel;
static GListModel *joined_rooms;
static GPtrArray *pushers;


static void
on_client_sync (gpointer   object,
                CmClient  *cm_client,
                CmRoom    *room,
                GPtrArray *events,
                GError    *err)
{
  g_debug ("Got new client events");

  if (room && events) {
    for (guint i = 0; i < events->len; i++) {
      CmRoomMessageEvent *event;

      event = events->pdata[i];
      g_debug ("Event type: %d", cm_event_get_m_type (CM_EVENT (event)));

      if (CM_IS_ROOM_MESSAGE_EVENT (event) &&
          cm_room_message_event_get_msg_type (CM_ROOM_MESSAGE_EVENT (event))) {
        g_debug ("text message: %s", cm_room_message_event_get_body (event));
      }
    }
  }

  if (err) {
    if (g_error_matches (err, CM_ERROR, CM_ERROR_BAD_PASSWORD)) {
      g_critical ("%s", err->message);
      ev_quit ();
      return;
    }
    g_warning ("client error (%d): %s", err->code, err->message);
  }
}


static void
on_joined_rooms_items_changed (GListModel *list,
                               guint       position,
                               guint       removed,
                               guint       added,
                               gpointer    user_data)
{
  g_debug ("Taking part in %d rooms", g_list_model_get_n_items (list));

  for (guint i = 0; i < g_list_model_get_n_items (list); i++) {
    g_autoptr (CmRoom) room = g_list_model_get_item (list, i);

    g_debug ("room name: %s, room id: %s\n",
             cm_room_get_name (room),
             cm_room_get_id (room));
  }
}


static char *
ev_enum_to_nick (GType g_enum_type, gint value)
{
  char *result;
  g_autoptr (GEnumClass) enum_class = NULL;
  GEnumValue *enum_value;

  g_return_val_if_fail (G_TYPE_IS_ENUM (g_enum_type), NULL);

  enum_class = g_type_class_ref (g_enum_type);

  /* Already warned */
  if (enum_class == NULL)
    return g_strdup_printf ("%d", value);

  enum_value = g_enum_get_value (enum_class, value);

  if (enum_value == NULL)
    result = g_strdup_printf ("%d", value);
  else
    result = g_strdup (enum_value->value_nick);

  return result;
}


static CmRoom *
get_joined_room_by_id (const char *room_id)
{
  CmRoom *room = NULL;

  for (guint i = 0; i < g_list_model_get_n_items (joined_rooms); i++) {
    g_autoptr (CmRoom) r = g_list_model_get_item (joined_rooms, i);
    const char *id = cm_room_get_id (r);

    if (g_str_equal (room_id, id)) {
      room = g_steal_pointer (&r);
      break;
    }
  }

  return room;
}


static void
on_matrix_open (GObject *object, GAsyncResult *result, gpointer user_data)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *username = NULL, *password = NULL, *config_path = NULL;
  g_autoptr (GKeyFile) keyfile = g_key_file_new ();
  GListModel *clients;

  if (!cm_matrix_open_finish (matrix, result, &err)) {
    g_critical ("Error opening db: %s", err->message);
    ev_quit ();
    return;
  }

  /* TODO: ask, libcmatrix saves them in the login keyring anyway */
  config_path = g_build_filename (g_get_user_config_dir (), EV_PROJECT, "accounts.cfg", NULL);

  if (!g_key_file_load_from_file (keyfile, config_path, G_KEY_FILE_NONE, &err)) {
    g_critical ("Failed to read config file %s: %s", config_path, err->message);
    ev_quit ();
    return;
  }

  username = g_key_file_get_string (keyfile, "matrix-00", "username", &err);
  if (!username) {
    g_critical ("Failed to get username: %s", err->message);
    ev_quit ();
    return;
  }
  password = g_key_file_get_string (keyfile, "matrix-00", "password", &err);
  if (!password) {
    g_critical ("Failed to get password: %s", err->message);
    ev_quit ();
    return;
  }

  clients = cm_matrix_get_clients_list (matrix);
  g_debug ("Found %d existing clients", g_list_model_get_n_items (clients));
  for (int i = 0; i < g_list_model_get_n_items (clients); i++) {
    g_autoptr (CmClient) c = g_list_model_get_item (clients, i);
    CmAccount *a = cm_client_get_account (c);

    /* See if we have a client with our id in the db already as we need to set
     * a sync callback for it as it will otherwise assert() */
    if (g_strcmp0 (cm_account_get_login_id (a), username) == 0) {
      client = g_steal_pointer (&c);
    } else {
      cm_client_set_enabled (c, FALSE);
    }
  }

  if (!client) {
    g_autoptr (GError) error = NULL;
    g_autofree char *homeserver = NULL;

    g_debug ("No client yet, creating a new one");
    client = cm_matrix_client_new (matrix);
    cm_client_set_password (client, password);
    cm_client_set_device_name (client, EV_PROJECT);

    homeserver = cm_utils_get_homeserver_sync (username, &error);
    if (!homeserver) {
      g_critical ("Could not determine homeserver for user '%s': %s",
                  username, error->message);
      ev_quit ();
      return;
    }
    cm_client_set_homeserver (client, homeserver);

    account = cm_client_get_account (client);
    if (!cm_account_set_login_id (account, username)) {
      g_critical ("'%s' isn't a valid username", username);
      ev_quit ();
      return;
    }

    if (!cm_matrix_save_client_sync (matrix, client, NULL, &error))
      g_warning ("Could not save client %p: %s", client, error->message);
  }
  cm_client_set_sync_callback (client, on_client_sync, NULL, NULL);

  cm_client_set_password (client, password);
  cm_client_set_device_name (client, EV_PROJECT);

  account = cm_client_get_account (client);
  if (!cm_account_set_login_id (account, username)) {
    g_critical ("'%s' isn't a valid username", username);
    ev_quit ();
    return;
  }

  g_print ("Logging in %s\n", username);
  cm_client_set_enabled (client, TRUE);
  joined_rooms = cm_client_get_joined_rooms (client);

  g_signal_connect_object (joined_rooms, "items-changed",
                           G_CALLBACK (on_joined_rooms_items_changed),
                           client,
                           G_CONNECT_DEFAULT);
}


void
ev_matrix_init (const char *data_dir, const char *cache_dir)
{
  cancel = g_cancellable_new ();

  matrix = cm_matrix_new (data_dir, cache_dir, EV_APP_ID, FALSE);
  cm_matrix_open_async (matrix, data_dir, "matrix.db", cancel, on_matrix_open, NULL);
}


void
ev_matrix_destroy (void)
{
  g_cancellable_cancel (cancel);
  g_clear_object (&cancel);

  g_clear_pointer (&pushers, g_ptr_array_unref);
  g_clear_object (&client);
  g_clear_object (&matrix);
}


static GString *
ev_matrix_list_rooms (GStrv unused, GError **err)
{
  g_autoptr (GString) out = g_string_new ("");

  if (!joined_rooms || g_list_model_get_n_items (joined_rooms) == 0) {
    g_string_append (out, "No joined rooms\n");
    return g_steal_pointer (&out);
  }

  for (guint i = 0; i < g_list_model_get_n_items (joined_rooms); i++) {
    g_autoptr (CmRoom) room = g_list_model_get_item (joined_rooms, i);

    g_string_append_printf (out, "  Room name: %s, room id: %s\n",
                            cm_room_get_name (room),
                            cm_room_get_id (room));
  }

  return g_steal_pointer (&out);
}


static GString *
ev_matrix_room_details (GStrv args, GError **err)
{
  g_autoptr (EvFormatBuilder) builder = ev_format_builder_new ();
  g_autoptr (CmRoom) room = NULL;
  const char *room_id;
  GListModel *events;

  g_assert (client);

  if (g_strv_length (args) < 1) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "Not enough arguments");
    return NULL;
  }
  room_id = args[0];

  room = get_joined_room_by_id (room_id);
  if (!room) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Room %s not found", room_id);
    return NULL;
  }

  ev_format_builder_set_indent (builder, INFO_INDENT);
  ev_format_builder_add (builder, _("Room Id"), cm_room_get_id (room));
  /* Translators: a matrix room name */
  ev_format_builder_add (builder, _("Name"), cm_room_get_name (room));
  ev_format_builder_add_nonnull (builder, _("Topic"), cm_room_get_topic (room));
  ev_format_builder_add (builder, _("Encrypted"),
                         cm_room_is_encrypted (room) ? _("Yes") : _("No"));
  ev_format_builder_take_value (builder, _("Unread notifications"),
                                g_strdup_printf ("%ld", cm_room_get_unread_notification_counts (room)));

  events = cm_room_get_events_list (room);
  ev_format_builder_take_value (builder, _("Events"),
                                g_strdup_printf ("%u", g_list_model_get_n_items (events)));


  return ev_format_builder_end (builder);
}


static GString *
ev_matrix_room_events (GStrv args, GError **err)
{
  g_autoptr (EvFormatBuilder) builder = ev_format_builder_new ();
  g_autoptr (CmRoom) room = NULL;
  const char *room_id;
  GListModel *events;

  g_assert (client);

  if (g_strv_length (args) < 1) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "Not enough arguments");
    return NULL;
  }
  room_id = args[0];

  room = get_joined_room_by_id (room_id);
  if (!room) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Room %s not found", room_id);
    return NULL;
  }

  events = cm_room_get_events_list (room);

  ev_format_builder_set_indent (builder, INFO_INDENT);

  ev_format_builder_take_value (builder, _("Events"),
                                g_strdup_printf ("%u", g_list_model_get_n_items (events)));
  for (guint i = 0; i < g_list_model_get_n_items (events); i++) {
    g_autoptr (CmEvent) event = g_list_model_get_item (events, i);
    CmEventType type = cm_event_get_m_type (event);

    ev_format_builder_add_newline (builder);
    ev_format_builder_add (builder, _("Event Id"), cm_event_get_id (CM_EVENT (event)));
    /* Translators: A matrix message event type */
    ev_format_builder_take_value (builder, _("Type"), ev_enum_to_nick (CM_TYPE_EVENT_TYPE, type));

    if (type == CM_M_ROOM_MESSAGE) {
      CmContentType content_type;
      CmRoomMessageEvent *rev = CM_ROOM_MESSAGE_EVENT (event);

      content_type = cm_room_message_event_get_msg_type (rev);
      ev_format_builder_take_value (builder, _("Content-Type"), ev_enum_to_nick (CM_TYPE_CONTENT_TYPE,
                                                                                 content_type));
      if (content_type == CM_CONTENT_TYPE_TEXT) {
        ev_format_builder_add (builder, _("Body"),
                               cm_room_message_event_get_body (rev));
      }
    }
  }

  return ev_format_builder_end (builder);
}


static GString *
ev_matrix_client_details (GStrv unused, GError **err)
{
  g_autoptr (EvFormatBuilder) builder = ev_format_builder_new ();
  gboolean logged_in;
  const char *device_id;

  if (!client)
    return NULL;

  device_id = cm_client_get_device_id (client);
  ev_format_builder_set_indent (builder, INFO_INDENT);
  ev_format_builder_add (builder, _("User"), cm_client_get_user_id (client));
  ev_format_builder_add (builder, _("Home server"), cm_client_get_homeserver (client));
  ev_format_builder_add (builder, _("Device ID"), device_id ?: "not logged in");
  if (device_id) {
    g_autoptr (GString) fp = NULL;
    const char *str;

    fp = g_string_new (NULL);
    str = cm_client_get_ed25519_key (client);

    while (str && *str) {
      g_autofree char *chunk = g_strndup (str, 4);

      g_string_append_printf (fp, "%s ", chunk);
      str = str + strlen (chunk);
    }
    ev_format_builder_add (builder, _("Fingerprint"), fp->str);
  }
  logged_in = cm_client_get_logged_in (client);
  ev_format_builder_add (builder, _("Logged in"), logged_in ? _("yes") : _("no"));
  if (!logged_in)
    ev_format_builder_add (builder, _("Logging in"), cm_client_get_logging_in (client) ? _("yes") : _("no"));

  return ev_format_builder_end (builder);
}


static GString *
ev_matrix_room_load_past_events (GStrv args, GError **err)
{
  g_autoptr (GError) local_err = NULL;
  g_autoptr (CmRoom) room = NULL;
  const char *room_id;
  gboolean success;

  g_assert (client);

  if (g_strv_length (args) < 1) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "Not enough arguments");
    return NULL;
  }
  room_id = args[0];

  room = get_joined_room_by_id (room_id);
  if (!room) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Room %s not found", room_id);
    return NULL;
  }

  success = cm_room_load_past_events_sync (room, &local_err);
  if (!success) {
    if (local_err) {
      g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED,
                   _("Failed to load events: %s"), local_err->message);
      return NULL;
    } else {
      g_string_new (_("No events loaded from database"));
    }
  }

  return g_string_new (_("Loaded events from database"));
}


static GString *
ev_matrix_room_get_event (GStrv args, GError **err)
{
  g_autoptr (GError) local_err = NULL;
  g_autoptr (CmEvent) event = NULL;
  g_autoptr (GString) out = g_string_new ("");
  g_autoptr (CmRoom) room = NULL;
  g_autofree char *nick = NULL;
  const char *room_id, *event_id;
  CmUser *user;
  GListModel *events;

  g_assert (client);

  if (g_strv_length (args) < 2) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "Not enough arguments");
    return NULL;
  }
  room_id = args[0];
  event_id = args[1];

  room = get_joined_room_by_id (room_id);
  if (!room) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Room %s not found", room_id);
    return NULL;
  }

  events = cm_room_get_events_list (room);
  for (guint i = 0; i < g_list_model_get_n_items (events); i++) {
    g_autoptr (CmEvent) e = g_list_model_get_item (events, i);
    const char *id = cm_event_get_id (e);

    if (g_str_equal (event_id, id)) {
      event = g_steal_pointer (&e);
      g_string_append_printf (out, "  Found cached event %s\n", id);
      break;
    }
  }

  if (event)
    goto print;

  event = cm_room_get_event_sync (room, event_id, cancel, &local_err);
  if (!event && local_err) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Failed to get event: %s", local_err->message);
    return NULL;
  }
  if (!event) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                 "Event %s not found", event_id);
    return NULL;
  }

 print:
  nick = ev_enum_to_nick (CM_TYPE_EVENT_TYPE, cm_event_get_m_type (event));
  user = cm_event_get_sender (event);
  g_string_append_printf (out, "    Message type: %s\n", nick);
  g_string_append_printf (out, "       Sender id: %s\n", cm_user_get_id (user));

  if (CM_IS_ROOM_MESSAGE_EVENT (event)) {
    CmRoomMessageEvent *room_msg = CM_ROOM_MESSAGE_EVENT (event);

    if (cm_room_message_event_get_msg_type (room_msg))
      g_string_append_printf (out, "       Text message: %s",
                              cm_room_message_event_get_body (room_msg));
  }

  return g_steal_pointer (&out);
}


static GString *
ev_matrix_get_pushers (GStrv args, GError **err)
{
  g_autoptr (GError) local_err = NULL;
  g_autoptr (EvFormatBuilder) builder = NULL;

  g_assert (CM_IS_CLIENT (client));

  g_clear_pointer (&pushers, g_ptr_array_unref);
  pushers = cm_client_get_pushers_sync (client, cancel, &local_err);
  if (!pushers) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Failed to get pushers: %s", local_err->message);
    return NULL;
  }

  if (!pushers->len)
    return g_string_new ("    No pushers configured\n");

  builder = ev_format_builder_new ();
  ev_format_builder_set_indent (builder, INFO_INDENT);
  for (int i = 0; i < pushers->len; i++) {
    CmPusher *pusher = g_ptr_array_index (pushers, i);

    if (i != 0)
      ev_format_builder_add_newline (builder);

    ev_format_builder_take_value (builder, "Pusher Id",
                                  g_strdup_printf ("%d", i));
    ev_format_builder_add (builder, "Kind",
                           cm_pusher_get_kind_as_string (pusher));
    ev_format_builder_add (builder, "App Display Name",
                           cm_pusher_get_app_display_name (pusher));
    ev_format_builder_add (builder, "App Id", cm_pusher_get_app_id (pusher));
    ev_format_builder_add (builder, "Device Display Name",
                           cm_pusher_get_device_display_name (pusher));
    ev_format_builder_add (builder, "Lang", cm_pusher_get_lang (pusher));
    ev_format_builder_add (builder, "Profile Tag", cm_pusher_get_profile_tag (pusher));
    ev_format_builder_add (builder, "Pushkey", cm_pusher_get_pushkey (pusher));
    if (cm_pusher_get_kind (pusher) == CM_PUSHER_KIND_HTTP)
      ev_format_builder_add (builder, "Url", cm_pusher_get_url (pusher));
  }

  return ev_format_builder_end (builder);
}


static GString *
ev_matrix_remove_pusher (GStrv args, GError **err)
{
  g_autoptr (GError) local_err = NULL;
  gint64 pusher_id;
  CmPusher *pusher;
  gboolean success;
  char *endptr;

  g_assert (CM_IS_CLIENT (client));

  if (g_strv_length (args) < 1) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "Not enough arguments");
    return NULL;
  }
  pusher_id = strtoll (args[0], &endptr, 10);
  errno = 0;
  if (errno) {
    g_set_error (err, G_IO_ERROR, g_io_error_from_errno (errno), "Invalid argument '%s'", args[0]);
    return NULL;
  }
  if (args[0] == endptr) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "No numbers found in '%s'", args[0]);
    return NULL;
  }

  if (!pushers) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "No pushers - did you run /get-pushers ?");
    return NULL;
  }

  if (pusher_id >= pushers->len || pusher_id < 0) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED, "Invalid pusher id '%ld'", pusher_id);
    return NULL;
  }

  pusher = g_ptr_array_index (pushers, pusher_id);
  g_assert (CM_IS_PUSHER (pusher));

  success = cm_client_remove_pusher_sync (client, pusher, cancel, &local_err);
  if (!success) {
    g_set_error (err, G_IO_ERROR, G_IO_ERROR_FAILED,
                 "Failed to remove pusher: %s", local_err->message);
    return NULL;
  }

  return g_string_new_take (g_strdup_printf ("Removed pusher %ld", pusher_id));
}


static GStrv
matrix_command_opt_get_room_completion (const char *word, int pos)
{
  g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();

  if (!joined_rooms)
    return NULL;

  for (guint i = 0; i < g_list_model_get_n_items (joined_rooms); i++) {
    g_autoptr (CmRoom) room = g_list_model_get_item (joined_rooms, i);
    const char *id = cm_room_get_id (room);

    if (strncmp (id, word, pos) == 0)
      g_strv_builder_add (builder, id);
  }

  return g_strv_builder_end (builder);
}


static const EvCmdOpt matrix_room_events_opts[] = {
  {
    .name = "room-id",
    .desc = "The id of the room to show the events for",
    .completer = matrix_command_opt_get_room_completion,
  },
  /* Sentinel */
  { NULL }
};


static const EvCmdOpt matrix_room_load_past_events_opts[] = {
  {
    .name = "room-id",
    .desc = "The id of the room to load the events for",
    .completer = matrix_command_opt_get_room_completion,
  },
  /* Sentinel */
  { NULL }
};


static const EvCmdOpt matrix_room_get_event_opts[] = {
  {
    .name = "room-id",
    .desc = "The id of the room to get the event for",
    .completer = matrix_command_opt_get_room_completion,
  },
  {
    .name = "event-id",
    .desc = "The id of the event to get",
  },
  /* Sentinel */
  { NULL }
};


static const EvCmdOpt matrix_room_details_opts[] = {
  {
    .name = "room-id",
    .desc = "The id of the room to get the details for",
    .completer = matrix_command_opt_get_room_completion,
  },
  /* Sentinel */
  { NULL }
};


static const EvCmdOpt matrix_get_remove_pusher_opts[] = {
  {
    .name = "number",
    .desc = "The number of the pusher",
  },
  /* Sentinel */
  { NULL }
};


static EvCmd matrix_commands[] = {
  {
    .name = "client-details",
    .help_summary = N_("Print client information - no request is made to the server"),
    .func = ev_matrix_client_details,
  },
  {
    .name = "rooms",
    .help_summary = N_("List currently known joined rooms - no request is made to the server"),
    .func = ev_matrix_list_rooms,
  },
  {
    .name = "room-details",
    .help_summary = N_("Get details about a room - no request is made to the server"),
    .func = ev_matrix_room_details,
    .opts = matrix_room_details_opts,
  },
  {
    .name = "room-events",
    .help_summary = N_("List events in a room"),
    .func = ev_matrix_room_events,
    .opts = matrix_room_events_opts,
  },
  {
    .name = "room-load-past-events",
    .help_summary = N_("Fetch past room events from the database"),
    .func = ev_matrix_room_load_past_events,
    .opts = matrix_room_load_past_events_opts,
  },
  {
    .name = "room-get-event",
    .help_summary = N_("Get the given event from the server"),
    .func = ev_matrix_room_get_event,
    .opts = matrix_room_get_event_opts,
  },
  {
    .name = "get-pushers",
    .help_summary = N_("Get the currently configured push servers from the server"),
    .func = ev_matrix_get_pushers,
  },
  {
    .name = "remove-pusher",
    .help_summary = N_("Remove the pusher with the given id"),
    .func = ev_matrix_remove_pusher,
    .opts = matrix_get_remove_pusher_opts,
  },
  /* Sentinel */
  { NULL }
};


void
ev_matrix_add_commands (GPtrArray *commands_)
{
  for (int i = 0; matrix_commands[i].name; i++)
    g_ptr_array_add (commands_, &matrix_commands[i]);
}
