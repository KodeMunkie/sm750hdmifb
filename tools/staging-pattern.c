/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* Ordinary WM-managed fullscreen window; never acquires DRM master. */
int main(void)
{
	Display *d = XOpenDisplay(NULL);
	if (!d) { fputs("Cannot open desktop display\n", stderr); return 1; }
	int screen = DefaultScreen(d);
	unsigned int w = DisplayWidth(d, screen), h = DisplayHeight(d, screen);
	Window root = RootWindow(d, screen);
	Window win = XCreateSimpleWindow(d, root, 0, 0, w, h, 0, 0, 0);
	XStoreName(d, win, "SM750 DMA batch benchmark — closes automatically");
	Atom fullscreen = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
	Atom state = XInternAtom(d, "_NET_WM_STATE", False);
	XChangeProperty(d, win, state, XA_ATOM, 32, PropModeReplace,
			(unsigned char *)&fullscreen, 1);
	XSelectInput(d, win, StructureNotifyMask | KeyPressMask);
	XMapRaised(d, win);
	XSync(d, False);
	struct timespec pause = { .tv_sec = 1, .tv_nsec = 0 };
	nanosleep(&pause, NULL);
	XWindowAttributes attrs;
	XGetWindowAttributes(d, win, &attrs);
	if ((unsigned int)attrs.width != w || (unsigned int)attrs.height != h) {
		fputs("Window manager did not provide fullscreen size\n", stderr);
		XCloseDisplay(d); return 1;
	}
	XImage *image = XCreateImage(d, DefaultVisual(d, screen),
		DefaultDepth(d, screen), ZPixmap, 0, NULL, w, h, 32, 0);
	if (!image) { XCloseDisplay(d); return 1; }
	image->data = calloc(h, image->bytes_per_line);
	if (!image->data) { XDestroyImage(image); XCloseDisplay(d); return 1; }
	GC gc = XCreateGC(d, win, 0, NULL);
	pause.tv_sec = 0; pause.tv_nsec = 250000000;
	for (unsigned int frame = 0; frame < 44; frame++) {
		while (XPending(d)) {
			XEvent event; XNextEvent(d, &event);
			if (event.type == KeyPress) {
				XDestroyImage(image); XCloseDisplay(d); return 2;
			}
		}
		for (unsigned int y = 0; y < h; y++)
			for (unsigned int x = 0; x < w; x++) {
				unsigned long value = ((x / 64 + y / 64 + frame) & 1) ?
					0x305070UL : 0xd0b090UL;
				XPutPixel(image, x, y, value);
			}
		XPutImage(d, win, gc, image, 0, 0, 0, 0, w, h);
		XSync(d, False);
		nanosleep(&pause, NULL);
	}
	XDestroyImage(image); XFreeGC(d, gc); XDestroyWindow(d, win);
	XCloseDisplay(d);
	return 0;
}
