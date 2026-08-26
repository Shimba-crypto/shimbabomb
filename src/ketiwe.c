#include "ketiwe.h"
#ifndef SB_NO_GUI
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static Display *dpy = NULL;
static Window win = 0;
static GC gc = 0;
static Pixmap backbuf = 0;
static GC backgc = 0;
static XFontStruct *font = NULL;
static int win_w = 0, win_h = 0;
static int mouse_x = -1, mouse_y = -1;
static int mouse_down = 0;
static int click_x = -1, click_y = -1;
static int should_close = 0;

int ketiwe_init(void) {
    if (dpy) return 1;
    dpy = XOpenDisplay(NULL);
    if (!dpy) return 0;
    font = XLoadQueryFont(dpy, "fixed");
    if (!font) font = XLoadQueryFont(dpy, "9x15");
    return dpy != NULL;
}

int ketiwe_window(const char *title, int w, int h) {
    if (!ketiwe_init()) return 0;
    if (win) ketiwe_close();
    int scr = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 100, 100, w, h, 2,
                              BlackPixel(dpy, scr), 0x12081f);
    win_w = w; win_h = h;
    XStoreName(dpy, win, title);
    XSelectInput(dpy, win, ExposureMask | ButtonPressMask | ButtonReleaseMask |
                           PointerMotionMask | StructureNotifyMask | KeyPressMask);
    Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wmDelete, 1);
    gc = XCreateGC(dpy, win, 0, NULL);
    if (font) XSetFont(dpy, gc, font->fid);
    backbuf = XCreatePixmap(dpy, win, w, h, DefaultDepth(dpy, scr));
    backgc = XCreateGC(dpy, backbuf, 0, NULL);
    if (font) XSetFont(dpy, backgc, font->fid);
    XSetForeground(dpy, backgc, 0x12081f);
    XFillRectangle(dpy, backbuf, backgc, 0, 0, w, h);
    XMapWindow(dpy, win);
    XFlush(dpy);
    should_close = 0;
    mouse_x = -1; mouse_y = -1;
    click_x = -1; click_y = -1;
    return 1;
}

void ketiwe_rect(int x, int y, int w, int h, unsigned color) {
    if (!dpy || !win || !backgc) return;
    XSetForeground(dpy, backgc, color & 0xFFFFFF);
    XFillRectangle(dpy, backbuf, backgc, x, y, w, h);
}

void ketiwe_text(int x, int y, const char *text) {
    if (!dpy || !win || !backgc || !text) return;
    XSetForeground(dpy, backgc, 0xf4f0ff);
    if (font) XSetFont(dpy, backgc, font->fid);
    XDrawString(dpy, backbuf, backgc, x, y, text, strlen(text));
}

int ketiwe_button(int x, int y, int w, int h, const char *label) {
    if (!dpy || !win || !backgc) return 0;
    int hover = (mouse_x >= x && mouse_x < x+w && mouse_y >= y && mouse_y < y+h);
    int clicked = (click_x >= x && click_x < x+w && click_y >= y && click_y < y+h);
    unsigned bg = hover ? 0x7c5ce0 : 0xa78bfa;
    XSetForeground(dpy, backgc, 0xffffff);
    XDrawRectangle(dpy, backbuf, backgc, x, y, w, h);
    XSetForeground(dpy, backgc, bg);
    XFillRectangle(dpy, backbuf, backgc, x+1, y+1, w-2, h-2);
    if (label) {
        int len = strlen(label);
        int fx = x + (w - len*8)/2;
        int fy = y + h/2 + 5;
        XSetForeground(dpy, backgc, 0x12081f);
        if (font) XSetFont(dpy, backgc, font->fid);
        XDrawString(dpy, backbuf, backgc, fx, fy, label, len);
    }
    return clicked ? 1 : 0;
}

int ketiwe_poll(void) {
    if (!dpy || !win) return 1;
    click_x = -1; click_y = -1;
    while (XPending(dpy)) {
        XEvent ev;
        XNextEvent(dpy, &ev);
        if (ev.type == Expose) {
            XCopyArea(dpy, backbuf, win, gc, 0, 0, win_w, win_h, 0, 0);
            XFlush(dpy);
        } else if (ev.type == MotionNotify) {
            mouse_x = ev.xmotion.x;
            mouse_y = ev.xmotion.y;
        } else if (ev.type == ButtonPress) {
            mouse_down = 1;
            mouse_x = ev.xbutton.x;
            mouse_y = ev.xbutton.y;
            click_x = ev.xbutton.x;
            click_y = ev.xbutton.y;
        } else if (ev.type == ButtonRelease) {
            mouse_down = 0;
        } else if (ev.type == ClientMessage) {
            should_close = 1;
        } else if (ev.type == DestroyNotify) {
            should_close = 1;
        } else if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Escape) should_close = 1;
        }
    }
    return should_close;
}

void ketiwe_flip(void) {
    if (!dpy || !win || !backbuf) return;
    XCopyArea(dpy, backbuf, win, gc, 0, 0, win_w, win_h, 0, 0);
    XFlush(dpy);
}

int ketiwe_mouse_x(void) { return mouse_x; }
int ketiwe_mouse_y(void) { return mouse_y; }
int ketiwe_mouse_down(void) { return mouse_down; }

void ketiwe_close(void) {
    if (dpy) {
        if (backbuf) { XFreePixmap(dpy, backbuf); backbuf = 0; }
        if (backgc) { XFreeGC(dpy, backgc); backgc = 0; }
        if (win) { XDestroyWindow(dpy, win); win = 0; }
        if (gc) { XFreeGC(dpy, gc); gc = 0; }
    }
    should_close = 0;
}
#else
int ketiwe_init(void){return 0;}
int ketiwe_window(const char *t,int w,int h){(void)t;(void)w;(void)h;return 0;}
void ketiwe_rect(int x,int y,int w,int h,unsigned c){(void)x;(void)y;(void)w;(void)h;(void)c;}
void ketiwe_text(int x,int y,const char *t){(void)x;(void)y;(void)t;}
int ketiwe_button(int x,int y,int w,int h,const char *l){(void)x;(void)y;(void)w;(void)h;(void)l;return 0;}
int ketiwe_poll(void){return 1;}
void ketiwe_flip(void){}
void ketiwe_close(void){}
#endif
