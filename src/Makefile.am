bin_PROGRAMS = osso-abook-home-applet

osso_abook_home_applet_CFLAGS = \
			$(APPLET_CFLAGS) \
			-DOSSO_ABOOK_DEBUG \
			$(MAEMO_LAUNCHER_CFLAGS)

osso_abook_home_applet_LDFLAGS = \
			-Wl,--as-needed $(APPLET_LIBS) \
			$(MAEMO_LAUNCHER_LDFLAGS)

osso_abook_home_applet_SOURCES = \
			main.c \
			osso-abook-home-applet.c

MAINTAINERCLEANFILES = Makefile.in
