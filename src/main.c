/*
 * main.c
 *
 * Copyright (C) 2022 Ivaylo Dimitrov <ivo.g.dimitrov.75@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <libosso-abook/osso-abook-init.h>
#include <libhildondesktop/hd-shortcuts.h>
#include <gconf/gconf-client.h>

#include "osso-abook-home-applet.h"

static DBusHandlerResult
dsme_dbus_filter(DBusConnection *connection, DBusMessage *message,
                 void *user_data)
{
  if (dbus_message_is_signal(message, "com.nokia.dsme.signal", "shutdown_ind"))
    exit(0);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int
main(int argc, char **argv, const char **envp)
{
  osso_context_t *osso;
  int rv = 0;
  GConfClient *gconf;
  GError *error = NULL;

  gdk_threads_init();
  osso = osso_initialize(PACKAGE_NAME, PACKAGE_VERSION, TRUE, NULL);

  if (!osso)
  {
    g_critical("Error initializing osso\n");
    return 1;
  }

  if (osso_abook_init_with_args(&argc, &argv, osso, NULL, NULL, NULL, &error))
  {
    DBusConnection *dbus = dbus_bus_get(DBUS_BUS_SYSTEM, NULL);
    HDShortcuts *shortcuts;

    if (dbus)
    {
      dbus_bus_add_match(
            dbus, "type='signal', interface='com.nokia.dsme.signal'", NULL);
      dbus_connection_add_filter(dbus, dsme_dbus_filter, NULL, NULL);
    }

    gconf = gconf_client_get_default();
    gconf_client_add_dir(gconf,
                         "/apps/osso-addressbook",
                         GCONF_CLIENT_PRELOAD_NONE,
                         NULL);
    shortcuts = hd_shortcuts_new("/apps/osso-addressbook/home-applets",
                                 OSSO_ABOOK_TYPE_HOME_APPLET);
    gtk_main();
    g_object_unref(shortcuts);
    g_object_unref(gconf);
  }
  else
  {
    if (error && (G_OPTION_ERROR == error->domain))
    {
      g_printerr("Usage error: %s\n", error->message);
      g_error_free(error);
      rv = 2;
    }
    else
    {
      g_critical("Unable to initialize libosso-abook");
      g_clear_error(&error);
      rv = 1;
    }
  }

  osso_deinitialize(osso);

  return rv;
}
