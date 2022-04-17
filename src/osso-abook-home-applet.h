/*
 * osso-abook-home-applet.h
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

#ifndef __OSSO_ABOOK_HOME_APPLET_H_INCLUDED__
#define __OSSO_ABOOK_HOME_APPLET_H_INCLUDED__

#include <libhildondesktop/hd-home-plugin-item.h>

G_BEGIN_DECLS

#define OSSO_ABOOK_TYPE_HOME_APPLET \
                (osso_abook_home_applet_get_type ())
#define OSSO_ABOOK_HOME_APPLET(obj) \
                (G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                 OSSO_ABOOK_TYPE_HOME_APPLET, \
                 OssoABookHomeApplet))
#define OSSO_ABOOK_HOME_APPLET_CLASS(cls) \
                (G_TYPE_CHECK_CLASS_CAST ((cls), \
                 OSSO_ABOOK_TYPE_HOME_APPLET, \
                 OssoABookHomeAppletClass))
#define OSSO_ABOOK_IS_HOME_APPLET(obj) \
                (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                 OSSO_ABOOK_TYPE_HOME_APPLET))
#define OSSO_ABOOK_IS_HOME_APPLET_CLASS(obj) \
                (G_TYPE_CHECK_CLASS_TYPE ((obj), \
                 OSSO_ABOOK_TYPE_HOME_APPLET))
#define OSSO_ABOOK_HOME_APPLET_GET_CLASS(obj) \
                (G_TYPE_INSTANCE_GET_CLASS ((obj), \
                 OSSO_ABOOK_TYPE_HOME_APPLET, \
                 OssoABookHomeAppletClass))

struct _OssoABookHomeApplet
{
  HDHomePluginItem parent;
};

typedef struct _OssoABookHomeApplet OssoABookHomeApplet;

struct _OssoABookHomeAppletClass
{
  HDHomePluginItemClass parent_class;
};

typedef struct _OssoABookHomeAppletClass OssoABookHomeAppletClass;

GType
osso_abook_home_applet_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* __OSSO_ABOOK_HOME_APPLET_H_INCLUDED__ */
