/*
 * osso-abook-home-applet.c
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

#include <hildon/hildon.h>
#include <libosso-abook/osso-abook-aggregator.h>
#include <libosso-abook/osso-abook-contact-subscriptions.h>
#include <libosso-abook/osso-abook-debug.h>
#include <libosso-abook/osso-abook-icon-sizes.h>
#include <libosso-abook/osso-abook-init.h>
#include <libosso-abook/osso-abook-presence-icon.h>
#include <libosso-abook/osso-abook-touch-contact-starter.h>
#include <libosso-abook/osso-abook-waitable.h>

#include "osso-abook-home-applet.h"

struct _OssoABookHomeAppletPrivate
{
  OssoABookAggregator *aggregator;
  OssoABookContact *contact;
  gchar *uid;
  GdkPixbuf *avatar_image;
  GtkWidget *fixed;
  GtkWidget *image;
  GtkWidget *presence_icon;
  GtkWidget *label;
  gulong contacts_removed_id;
  gulong contacts_added_id;
  guint respawn_id;
  int flags;
};

typedef struct _OssoABookHomeAppletPrivate OssoABookHomeAppletPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(
  OssoABookHomeApplet,
  osso_abook_home_applet,
  HD_TYPE_HOME_PLUGIN_ITEM
);

#define PRIVATE(applet) \
  ((OssoABookHomeAppletPrivate *) \
   osso_abook_home_applet_get_instance_private((OssoABookHomeApplet *) \
                                               (applet)))

static void
aggregator_weak_notify(void *data, GObject *where_the_object_was);

static OssoABookAggregator *aggregator = NULL;
static guint respawn_count = 0;
static guint respawn_timeout_id;
static guint idle_update_id = 0;
static GList *applets = NULL;

static GtkWidget *dialog = NULL;

static GdkPixbuf *frame_active_pixbuf = NULL;
static GdkPixbuf *frame_pixbuf = NULL;
static cairo_surface_t *avatar_mask = NULL;
static GtkStyle *style = NULL;

static gboolean
abook_backend_died_cd(EBook *book, gpointer user_data)
{
  ESource *source = e_book_get_source(book);

  if (++respawn_count > 3)
  {
    g_critical("Backend for %s died and respawn limit exceeded. Aborting.",
               e_source_get_uid(source));
    return FALSE;
  }

  g_warning("Backend for %s died. Starting over.", e_source_get_uid(source));

  if (aggregator)
    g_object_unref(aggregator);

  aggregator = NULL;

  return TRUE;
}

static OssoABookContactSubscriptions *
get_contact_subscriptions()
{
  static OssoABookContactSubscriptions *contact_subscriptions = NULL;

  if (!contact_subscriptions)
    contact_subscriptions = osso_abook_contact_subscriptions_new();

  return contact_subscriptions;
}

static void
contact_notify_avatar_image_cb(OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  if (priv->avatar_image)
  {
    g_object_unref(priv->avatar_image);
    priv->avatar_image = NULL;
  }

  if (priv->contact)
  {
    priv->avatar_image = osso_abook_avatar_get_image_scaled(
        OSSO_ABOOK_AVATAR(priv->contact),
        OSSO_ABOOK_PIXEL_SIZE_AVATAR_MEDIUM,
        OSSO_ABOOK_PIXEL_SIZE_AVATAR_MEDIUM,
        TRUE);
  }

  if (!priv->avatar_image)
  {
    if (priv->contact && OSSO_ABOOK_IS_AVATAR(priv->contact))
    {
      const char *fallback_icon = osso_abook_avatar_get_fallback_icon_name(
          OSSO_ABOOK_AVATAR(priv->contact));

      if (fallback_icon)
      {
        priv->avatar_image = gtk_icon_theme_load_icon(
            gtk_icon_theme_get_default(), fallback_icon,
            OSSO_ABOOK_PIXEL_SIZE_AVATAR_MEDIUM, GTK_ICON_LOOKUP_USE_BUILTIN,
            NULL);
      }
    }
  }

  if (!priv->avatar_image)
  {
    priv->avatar_image = gtk_icon_theme_load_icon(
        gtk_icon_theme_get_default(), "general_default_avatar",
        OSSO_ABOOK_PIXEL_SIZE_AVATAR_MEDIUM, GTK_ICON_LOOKUP_USE_BUILTIN,
        NULL);
  }

  gtk_widget_queue_draw(GTK_WIDGET(applet));
}

static void
contact_notify_presence_type_cb(OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  if (osso_abook_presence_get_icon_name(OSSO_ABOOK_PRESENCE(priv->contact)))
    gtk_widget_show(priv->presence_icon);
  else
    gtk_widget_hide(priv->presence_icon);
}

static void
update_nickname(OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  EContact *ec = E_CONTACT(priv->contact);
  const gchar *nickname;

  nickname = e_contact_get_const(ec, E_CONTACT_NICKNAME);

  if (!nickname || !*nickname)
    nickname = e_contact_get_const(ec, E_CONTACT_GIVEN_NAME);

  if (!nickname || !*nickname)
    nickname = e_contact_get_const(ec, E_CONTACT_FAMILY_NAME);

  if (!nickname || !*nickname)
    nickname = e_contact_get_const(ec, E_CONTACT_ORG);

  if (!nickname || !*nickname)
    nickname = osso_abook_contact_get_display_name(priv->contact);

  OSSO_ABOOK_NOTE(GENERIC, "Update nickname to %s", nickname);
  gtk_label_set_text(GTK_LABEL(priv->label), nickname);
}

static void
remove_applet(OssoABookHomeApplet *applet)
{
  applets = g_list_remove(applets, applet);

  if (idle_update_id)
  {
    if (!applets)
    {
      g_source_remove(idle_update_id);
      idle_update_id = 0;
    }
  }
}

static void
update_contact(OssoABookHomeApplet *applet, OssoABookContact *contact)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  if (priv->contact)
  {
    g_signal_handlers_disconnect_matched(
      priv->contact, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      contact_notify_avatar_image_cb, applet);
    g_signal_handlers_disconnect_matched(
      priv->contact, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      contact_notify_presence_type_cb, applet);
    g_signal_handlers_disconnect_matched(
      priv->contact, G_SIGNAL_MATCH_DATA | G_SIGNAL_MATCH_FUNC, 0, 0, NULL,
      update_nickname, applet);
    g_object_unref(priv->contact);
    priv->contact = NULL;
  }

  if (contact)
  {
    remove_applet(applet);
    priv->contact = g_object_ref(contact);
    contact_notify_avatar_image_cb(applet);
    g_signal_connect_swapped(
      contact, "notify::avatar-image",
      G_CALLBACK(contact_notify_avatar_image_cb), applet);
    osso_abook_presence_icon_set_presence(
      OSSO_ABOOK_PRESENCE_ICON(priv->presence_icon),
      OSSO_ABOOK_PRESENCE(contact));
    contact_notify_presence_type_cb(applet);
    g_signal_connect_swapped(
      contact, "notify::presence-type",
      G_CALLBACK(contact_notify_presence_type_cb), applet);
    update_nickname(applet);
    g_signal_connect_swapped(contact, "reset",
                             G_CALLBACK(update_nickname), applet);
    gtk_widget_show(GTK_WIDGET(applet));
  }
}

static void
check_contacts(OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  OssoABookContact *contact = NULL;

  if (priv->aggregator)
  {
    GList *l = osso_abook_aggregator_lookup(priv->aggregator, priv->uid);

    if (l)
    {
      contact = l->data;
      g_list_free(l);
    }
  }

  update_contact(applet, contact);
}

static gboolean
idle_update_applets(gpointer user_data)
{
  GSList *home_applets = osso_abook_settings_get_home_applets();
  gboolean removed = FALSE;
  GList *applet = applets;

  while (applet)
  {
    OssoABookHomeAppletPrivate *priv = PRIVATE(applet->data);
    GSList *l;

    if (priv->contact)
    {
      applet = applet->next;
      break;
    }

    for (l = home_applets; l; l = l->next)
    {
      if (g_str_has_prefix(l->data, OSSO_ABOOK_HOME_APPLET_PREFIX))
      {
        if (!strcmp(
              (const char *)l->data + strlen(OSSO_ABOOK_HOME_APPLET_PREFIX),
              priv->uid))
        {
          g_free(l->data);
          removed = TRUE;
          home_applets = g_slist_delete_link(home_applets, l);
          break;
        }
      }
    }

    applets = g_list_delete_link(applets, applet);
    applet = applets;
  }

  if (removed)
    osso_abook_settings_set_home_applets(home_applets);

  g_slist_free_full(home_applets, g_free);
  idle_update_id = 0;

  return FALSE;
}

static void
update_applets(OssoABookHomeApplet *applet)
{
  if (!g_list_find(applets, applet))
    applets = g_list_prepend(applets, applet);

  if (!idle_update_id)
    idle_update_id = g_idle_add(idle_update_applets, NULL);
}

static void
contacts_removed_cb(OssoABookRoster *roster, const char **uids,
                    OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  const char *contact_uid = NULL;

  if (priv->contact)
    contact_uid = e_contact_get_const(E_CONTACT(priv->contact), E_CONTACT_UID);

  while (*uids)
  {
    if (!strcmp(*uids, priv->uid))
    {
      update_contact(applet, NULL);
      update_applets(applet);
      break;
    }

    if (contact_uid && !strcmp(*uids, contact_uid))
    {
      check_contacts(applet);

      if (!priv->contact)
        update_applets(applet);

      break;
    }

    uids++;
  }
}

static void
contacts_added_cb(OssoABookRoster *roster, OssoABookContact **contacts,
                  OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  if (!priv->contact)
    check_contacts(applet);
}

static void
aggregator_ready_cb(OssoABookWaitable *waitable, const GError *error,
                    gpointer user_data)
{
  OssoABookHomeApplet *applet = user_data;
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  priv->contacts_removed_id =
    g_signal_connect(priv->aggregator, "contacts-removed",
                     G_CALLBACK(contacts_removed_cb), applet);

  priv->contacts_added_id =
    g_signal_connect(priv->aggregator, "contacts-added",
                     G_CALLBACK(contacts_added_cb), applet);

  check_contacts(applet);
}

static void
create_aggregator(OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  if (!aggregator)
  {
    aggregator = OSSO_ABOOK_AGGREGATOR(osso_abook_aggregator_new(NULL, NULL));
    osso_abook_aggregator_add_filter(
      aggregator, OSSO_ABOOK_CONTACT_FILTER(get_contact_subscriptions()));
    osso_abook_roster_start(OSSO_ABOOK_ROSTER(aggregator));
  }

  priv->aggregator = aggregator;
  g_object_weak_ref(G_OBJECT(aggregator), aggregator_weak_notify, applet);
  osso_abook_waitable_call_when_ready(OSSO_ABOOK_WAITABLE(aggregator),
                                      aggregator_ready_cb, applet, NULL);
}

static gboolean
respawn_timeout_cb(gpointer user_data)
{
  respawn_timeout_id = 0;
  respawn_count = 0;

  return FALSE;
}

static gboolean
respawn_cb(gpointer user_data)
{
  OssoABookHomeApplet *applet = user_data;
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  if (respawn_timeout_id)
    g_source_remove(respawn_timeout_id);

  respawn_timeout_id = g_timeout_add(60000, respawn_timeout_cb, NULL);
  priv->respawn_id = 0;
  create_aggregator(applet);

  return FALSE;
}

static void
aggregator_weak_notify(void *data, GObject *where_the_object_was)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(data);

  if ((GObject *)priv->aggregator == where_the_object_was)
  {
    osso_abook_roster_manager_stop(osso_abook_roster_manager_get_default());

    priv->aggregator = NULL;
    priv->contacts_removed_id = 0;
    priv->contacts_added_id = 0;

    if (priv->respawn_id)
      g_source_remove(priv->respawn_id);

    priv->respawn_id = g_timeout_add(2500, respawn_cb, data);
  }
}

static void
osso_abook_home_applet_dispose(GObject *object)
{
  OssoABookHomeApplet *applet = OSSO_ABOOK_HOME_APPLET(object);
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  remove_applet(applet);

  if (priv->respawn_id)
  {
    g_source_remove(priv->respawn_id);
    priv->respawn_id = 0;
  }

  if (priv->aggregator)
  {
    g_object_weak_unref(G_OBJECT(priv->aggregator), aggregator_weak_notify,
                        applet);

    if (priv->contacts_removed_id)
    {
      g_signal_handler_disconnect(priv->aggregator, priv->contacts_removed_id);
      priv->contacts_removed_id = 0;
    }

    if (priv->contacts_added_id)
    {
      g_signal_handler_disconnect(priv->aggregator, priv->contacts_added_id);
      priv->contacts_added_id = 0;
    }

    priv->aggregator = NULL;
  }

  update_contact(applet, NULL);
  osso_abook_contact_subscriptions_remove(get_contact_subscriptions(),
                                          priv->uid);

  if (priv->avatar_image)
  {
    g_object_unref(priv->avatar_image);
    priv->avatar_image = NULL;
  }

  G_OBJECT_CLASS(osso_abook_home_applet_parent_class)->dispose(object);
}

static void
osso_abook_home_applet_finalize(GObject *object)
{
  OssoABookHomeApplet *applet = OSSO_ABOOK_HOME_APPLET(object);
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  g_free(priv->uid);

  G_OBJECT_CLASS(osso_abook_home_applet_parent_class)->finalize(object);
}

static void
osso_abook_home_applet_constructed(GObject *object)
{
  OssoABookHomeApplet *applet = OSSO_ABOOK_HOME_APPLET(object);
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  gchar *plugin_id;

  G_OBJECT_CLASS(osso_abook_home_applet_parent_class)->constructed(object);

  g_object_get(applet, "plugin-id", &plugin_id, NULL);

  if (g_str_has_prefix(plugin_id, OSSO_ABOOK_HOME_APPLET_PREFIX))
  {
    priv->uid = g_strdup(plugin_id + strlen(OSSO_ABOOK_HOME_APPLET_PREFIX));
    g_free(plugin_id);
  }
  else
  {
    g_warning("%s is not a valid abook home applet id", plugin_id);
    priv->uid = plugin_id;
  }

  osso_abook_contact_subscriptions_add(get_contact_subscriptions(), priv->uid);
  create_aggregator(applet);
}

static void
osso_abook_home_applet_screen_changed(GtkWidget *widget,
                                      GdkScreen *previous_screen)
{
  OssoABookHomeApplet *applet = OSSO_ABOOK_HOME_APPLET(widget);
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  GdkScreen *screen;
  GdkColormap *colormap;

  if (previous_screen)
  {
    GTK_WIDGET_CLASS(osso_abook_home_applet_parent_class)->screen_changed(
      widget, previous_screen);
  }

  screen = gtk_widget_get_screen(GTK_WIDGET(applet));
  colormap = gdk_screen_get_rgba_colormap(screen);

  if (colormap)
    priv->flags |= 1u;                  // has_alpha
  else
  {
    colormap = gdk_screen_get_rgb_colormap(screen);
    priv->flags &= ~1u;
  }

  gtk_widget_set_colormap(widget, colormap);
}

static void
osso_abook_home_applet_show(GtkWidget *widget)
{
  OssoABookHomeApplet *applet = OSSO_ABOOK_HOME_APPLET(widget);
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  if (priv->contact)
    GTK_WIDGET_CLASS(osso_abook_home_applet_parent_class)->show(widget);
}

static void
create_new_avatar_mask(GdkWindow *window)
{
  cairo_t *cr = gdk_cairo_create(window);
  cairo_surface_t *surface;

  surface = cairo_surface_create_similar(
      cairo_get_target(cr),
      CAIRO_CONTENT_ALPHA,
      cairo_image_surface_get_width(avatar_mask),
      cairo_image_surface_get_height(avatar_mask));
  cairo_destroy(cr);

  cr = cairo_create(surface);
  cairo_set_source_surface(cr, avatar_mask, 0.0, 0.0);
  cairo_paint(cr);
  cairo_destroy(cr);
  cairo_surface_destroy(avatar_mask);
  avatar_mask = surface;
}

static gboolean
osso_abook_home_applet_expose_event(GtkWidget *widget, GdkEventExpose *event)
{
  OssoABookHomeApplet *applet = OSSO_ABOOK_HOME_APPLET(widget);
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  cairo_t *cr = gdk_cairo_create(widget->window);

  gdk_cairo_region(cr, event->region);
  cairo_clip(cr);

  if (priv->flags & 1)
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.0);
  else
    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);

  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cr);

  if (cairo_surface_get_type(avatar_mask) == CAIRO_SURFACE_TYPE_IMAGE)
    create_new_avatar_mask(widget->window);

  if (priv->avatar_image && avatar_mask)
  {
    gdk_cairo_set_source_pixbuf(cr, priv->avatar_image, 8.0, 8.0);
    cairo_mask_surface(cr, avatar_mask, 8.0, 8.0);
  }

  cairo_destroy(cr);

  return GTK_WIDGET_CLASS(osso_abook_home_applet_parent_class)->expose_event(
           widget, event);
}

static GdkPixbuf *
load_pixbuf(GtkSettings *settings, gchar *pixmap_file)
{
  gchar *filename = gtk_rc_find_pixmap_in_path(settings, NULL, pixmap_file);
  GdkPixbuf *pixbuf;
  GError *error = NULL;

  g_return_val_if_fail(NULL != filename, NULL);

  pixbuf = gdk_pixbuf_new_from_file(filename, &error);

  if (error)
  {
    g_warning("%s: %s", __FUNCTION__, error->message);
    g_clear_error(&error);
  }

  g_free(filename);

  return pixbuf;
}

static void
osso_abook_home_applet_style_set(GtkWidget *widget, GtkStyle *previous_style)
{
  OssoABookHomeApplet *applet = OSSO_ABOOK_HOME_APPLET(widget);
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  GtkWidgetClass *widget_class =
    GTK_WIDGET_CLASS(osso_abook_home_applet_parent_class);
  gchar *filename;
  GtkStyle *new_style;

  if (widget_class->style_set)
    widget_class->style_set(widget, previous_style);

  new_style = gtk_widget_get_style(widget);

  if (new_style != style)
  {
    GtkSettings *settings;

    style = new_style;
    settings = gtk_settings_get_default();

    if (frame_pixbuf)
      g_object_unref(frame_pixbuf);

    frame_pixbuf = load_pixbuf(settings, "ContactsAppletFrame.png");

    g_assert(GDK_IS_PIXBUF(frame_pixbuf));

    if (frame_active_pixbuf)
      g_object_unref(frame_active_pixbuf);

    frame_active_pixbuf = load_pixbuf(settings,
                                      "ContactsAppletFrameActive.png");

    g_assert(GDK_IS_PIXBUF(frame_active_pixbuf));

    if (avatar_mask)
      cairo_surface_destroy(avatar_mask);

    filename = gtk_rc_find_pixmap_in_path(settings, NULL,
                                          "ContactsAppletMask.png");
    avatar_mask = cairo_image_surface_create_from_png(filename);

    g_assert(avatar_mask != NULL);

    g_free(filename);
  }

  gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), frame_pixbuf);
  gtk_widget_queue_draw(widget);
}

static void
osso_abook_home_applet_class_init(OssoABookHomeAppletClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = osso_abook_home_applet_dispose;
  object_class->finalize = osso_abook_home_applet_finalize;
  object_class->constructed = osso_abook_home_applet_constructed;

  widget_class->style_set = osso_abook_home_applet_style_set;
  widget_class->screen_changed = osso_abook_home_applet_screen_changed;
  widget_class->show = osso_abook_home_applet_show;
  widget_class->expose_event = osso_abook_home_applet_expose_event;

  osso_abook_set_backend_died_func(abook_backend_died_cd, NULL);
}

static gboolean
button_press_event_cb(GtkWidget *self, GdkEventButton *event,
                      OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), frame_active_pixbuf);

  return TRUE;
}

static gboolean
button_release_event_cb(GtkWidget *self, GdkEventButton *event,
                        OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  GtkWidget *starter;

  if (dialog)
    return FALSE;

  gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), frame_pixbuf);
  starter = osso_abook_touch_contact_starter_new_with_contact(
      NULL, priv->contact);

  dialog = osso_abook_touch_contact_starter_dialog_new(
      NULL, OSSO_ABOOK_TOUCH_CONTACT_STARTER(starter));
  g_object_add_weak_pointer(G_OBJECT(dialog), (gpointer *)&dialog);
  gtk_widget_show(starter);
  gtk_widget_show(dialog);

  return TRUE;
}

static gboolean
leave_notify_event_cb(GtkWidget *self, GdkEventCrossing *event,
                      OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);

  gtk_image_set_from_pixbuf(GTK_IMAGE(priv->image), frame_pixbuf);

  return FALSE;
}

static void
osso_abook_home_applet_init(OssoABookHomeApplet *applet)
{
  OssoABookHomeAppletPrivate *priv = PRIVATE(applet);
  GtkWidget *event_box;
  GtkWidget *align;
  GtkWidget *hbox;

  gtk_widget_add_events(GTK_WIDGET(applet), GDK_BUTTON_PRESS_MASK);
  gtk_widget_set_app_paintable(GTK_WIDGET(applet), TRUE);

  osso_abook_home_applet_screen_changed(GTK_WIDGET(applet), NULL);

  priv->fixed = gtk_fixed_new();
  gtk_container_add(GTK_CONTAINER(applet), priv->fixed);

  priv->image = gtk_image_new();
  gtk_fixed_put(GTK_FIXED(priv->fixed), priv->image, 0, 0);

  event_box = gtk_event_box_new();
  gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), FALSE);
  gtk_fixed_put(GTK_FIXED(priv->fixed), event_box, 0, 0);
  gtk_widget_set_size_request(event_box, 144, 178);

  g_signal_connect(event_box, "button-press-event",
                   G_CALLBACK(button_press_event_cb), applet);
  g_signal_connect(event_box, "button-release-event",
                   G_CALLBACK(button_release_event_cb), applet);
  g_signal_connect(event_box, "leave-notify-event",
                   G_CALLBACK(leave_notify_event_cb), applet);

  align = gtk_alignment_new(0.5, 0.5, 0.0, 0.0);
  gtk_fixed_put(GTK_FIXED(priv->fixed), align, 12, 140);
  gtk_widget_set_size_request(align, 120, 30);

  hbox = gtk_hbox_new(FALSE, 8);
  gtk_container_add(GTK_CONTAINER(align), hbox);

  priv->presence_icon = osso_abook_presence_icon_new(NULL);
  gtk_box_pack_start(GTK_BOX(hbox), priv->presence_icon, FALSE, FALSE, 0);
  gtk_widget_set_no_show_all(priv->presence_icon, TRUE);

  priv->label = gtk_label_new(NULL);
  gtk_widget_set_name(priv->label, "hildon-shadow-label");
  hildon_helper_set_logical_font(priv->label, "SmallSystemFont");
  gtk_box_pack_start(GTK_BOX(hbox), priv->label, TRUE, TRUE, 0);

  gtk_widget_show_all(GTK_WIDGET(priv->fixed));
}
