/*
 * Copyright (c) 2008, Thomas Jaeger <ThJaeger@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <cspi/spi.h>
#include <glib.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <map>
#include <boost/shared_ptr.hpp>

#define WIDTH 486
#define HEIGHT 112

static AccessibleEventListener *focus_listener;
static AccessibleEventListener *activate_listener;
static AccessibleEventListener *deactivate_listener;

typedef boost::shared_ptr<SPIRect> Rect;

std::map<int, Rect> focus_map;
int current_app;
bool active = false;

Display *dpy;
#define ROOT (DefaultRootWindow(dpy))
Window win;

gboolean query_pointer(int *x, int *y) {
	Window root, child;
	int win_x, win_y;
	unsigned int mask;
	int ret = XQueryPointer(dpy, ROOT, &root, &child,
				x, y, &win_x, &win_y, &mask);
	return ret;
}

void hide_cellwriter() {
	system("cellwriter --hide-window");
	active = false;
}

void show_cellwriter(Rect e) {
	int x, y;
	if (!query_pointer(&x, &y))
		return;
	if (active)
		hide_cellwriter();
	system("cellwriter --show-window");
	active = true;
}

void update(int app_id) {
	current_app = app_id;
	std::map<int, Rect>::iterator i = focus_map.find(app_id);
	if (i == focus_map.end())
		return;
	Rect e = i->second;
	if (e)
		show_cellwriter(e);
	else
		hide_cellwriter();
}

Rect get_extents(Accessible *obj) {
	AccessibleComponent *comp = Accessible_getComponent(obj);
	if (!comp)
		return Rect();
	Rect e(new SPIRect);
	AccessibleComponent_getExtents(comp, &e->x, &e->y, &e->width, &e->height, SPI_COORD_TYPE_SCREEN);
	AccessibleComponent_unref(comp);
	return e;
}

Rect get_text_extents(Accessible *obj) {
	if (!Accessible_isEditableText(obj))
		return Rect();
	AccessibleRole role = Accessible_getRole(obj);
	if (role == SPI_ROLE_DOCUMENT_FRAME)
		return Rect();
	return get_extents(obj);
}

typedef void(*SPIHandler)(const AccessibleEvent *, int app_id);

void on_focus(const AccessibleEvent *event, int app_id) {
	focus_map[app_id] = get_text_extents(event->source);
	update(app_id);
}

void on_activate(const AccessibleEvent *event, int app_id) {
	update(app_id);
}

void quit(int sig) {
	SPI_deregisterGlobalEventListenerAll(focus_listener);
	SPI_deregisterGlobalEventListenerAll(activate_listener);
	SPI_deregisterGlobalEventListenerAll(deactivate_listener);
	AccessibleEventListener_unref(focus_listener);
	AccessibleEventListener_unref(activate_listener);
	AccessibleEventListener_unref(deactivate_listener);
	SPI_event_quit();
}

void call(const AccessibleEvent *event, void *user_data) {
	if (event->source) {
		AccessibleApplication *app = AccessibleEvent_getSourceApplication(event);
		if (app) {
			((SPIHandler)user_data)(event, AccessibleApplication_getID(app));
			AccessibleApplication_unref(app);
		}
	}
	AccessibleEvent_unref(event);
}

AccessibleEventListener *create_listener(SPIHandler handler) {
	return SPI_createAccessibleEventListener(call, (gpointer)handler);
}

bool is_cw_win(Window w) {
	if (!w)
		return false;
	XClassHint ch;
	if (!XGetClassHint(dpy, w, &ch))
		return false;
	bool cw = !strcmp(ch.res_name, "cellwriter");
	XFree(ch.res_name);
	XFree(ch.res_class);
	if (!cw)
		return false;
	XWMHints *wm_hints = XGetWMHints(dpy, w);
	if (!wm_hints)
		return false;
	bool input = wm_hints->input;
	XFree(wm_hints);
	return !input;
}

Window find_cw_win(Window w, int depth) {
	if (!w)
		return None;
	if (is_cw_win(w))
		return w;
	if (!depth)
		return None;
	depth--;

	unsigned int n;
	Window dummyw1, dummyw2, *ch;
	if (!XQueryTree(dpy, w, &dummyw1, &dummyw2, &ch, &n))
		return None;
	for (unsigned int i = 0; i != n; i++) {
		Window found = find_cw_win(ch[i], depth);
		if (found) {
			XFree(ch);
			return found;
		}
	}
	XFree(ch);
	return None;
}

gboolean handle_x11(GIOChannel *src, GIOCondition cond, gpointer data) {
	XEvent ev;
	XNextEvent(dpy, &ev);
	return TRUE;
}

int main (int argc, char **argv) {
	if (SPI_init()) {
		printf("Error: AT-SPI not available\n");
		exit(EXIT_FAILURE);
	}

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		printf("Error: Can't connect to display\n");
		exit(EXIT_FAILURE);
	}
	
	win = find_cw_win(ROOT, 1);
	if (!win) {
		printf("Error: Cellwriter window not found\n");
		exit(EXIT_FAILURE);
	}

	GIOChannel *io_chan = g_io_channel_unix_new(ConnectionNumber(dpy));
	g_io_add_watch(io_chan, G_IO_IN, handle_x11, NULL);

	focus_listener = create_listener(&on_focus);
	activate_listener = create_listener(&on_activate);
	SPI_registerGlobalEventListener(focus_listener, "focus:");
	SPI_registerGlobalEventListener(activate_listener, "window:activate");
	signal(SIGINT, &quit);
	SPI_event_main();
	int garbage = SPI_exit();
	if (garbage)
		printf("Garbage: %d\n", garbage);
	return EXIT_SUCCESS;
}
