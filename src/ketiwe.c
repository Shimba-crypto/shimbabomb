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
static int key_pressed = 0;
static int last_key = 0;
static char input_bufs[8][256];
static int input_len[8];
static int input_focus = -1;

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
    if (w <= 0 || h <= 0) return;
    XSetForeground(dpy, backgc, color & 0xFFFFFF);
    XFillRectangle(dpy, backbuf, backgc, x, y, w, h);
}

void ketiwe_rect_outline(int x, int y, int w, int h, unsigned color) {
    if (!dpy || !win || !backgc) return;
    if (w <= 0 || h <= 0) return;
    XSetForeground(dpy, backgc, color & 0xFFFFFF);
    XDrawRectangle(dpy, backbuf, backgc, x, y, w, h);
}

void ketiwe_line(int x1, int y1, int x2, int y2, unsigned color) {
    if (!dpy || !win || !backgc) return;
    XSetForeground(dpy, backgc, color & 0xFFFFFF);
    XDrawLine(dpy, backbuf, backgc, x1, y1, x2, y2);
}

void ketiwe_clear(unsigned color) {
    if (!dpy || !win || !backgc) return;
    XSetForeground(dpy, backgc, color & 0xFFFFFF);
    XFillRectangle(dpy, backbuf, backgc, 0, 0, win_w, win_h);
}

void ketiwe_circle(int cx, int cy, int r, unsigned color) {
    if (!dpy || !win || !backgc) return;
    if (r <= 0) return;
    XSetForeground(dpy, backgc, color & 0xFFFFFF);
    XFillArc(dpy, backbuf, backgc, cx - r, cy - r, r * 2, r * 2, 0, 360 * 64);
}

void ketiwe_text(int x, int y, const char *text) {
    if (!dpy || !win || !backgc || !text) return;
    XSetForeground(dpy, backgc, 0xf4f0ff);
    if (font) XSetFont(dpy, backgc, font->fid);
    XDrawString(dpy, backbuf, backgc, x, y, text, strlen(text));
}

int ketiwe_button(int x, int y, int w, int h, const char *label) {
    if (!dpy || !win || !backgc) return 0;
    if (w < 3) w = 3;
    if (h < 3) h = 3;
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

int ketiwe_input(int x, int y, int w, int h, char *buf, int bufsize, const char *placeholder) {
    if (!dpy || !win || !backgc) return 0;
    int idx = 0;
    for (int i = 0; i < 8; i++) {
        if (input_bufs[i] == buf || (input_len[i] == 0 && input_bufs[i][0] == '\0')) { idx = i; break; }
        if (i == 7) idx = 7;
    }
    // background
    XSetForeground(dpy, backgc, 0x1e1e2e);
    XFillRectangle(dpy, backbuf, backgc, x, y, w, h);
    // border
    unsigned border = (input_focus == idx) ? 0xa78bfa : 0x45475a;
    XSetForeground(dpy, backgc, border);
    XDrawRectangle(dpy, backbuf, backgc, x, y, w, h);
    // text
    if (input_len[idx] > 0) {
        XSetForeground(dpy, backgc, 0xf4f0ff);
        if (font) XSetFont(dpy, backgc, font->fid);
        XDrawString(dpy, backbuf, backgc, x + 6, y + h/2 + 5, input_bufs[idx], input_len[idx]);
    } else if (placeholder) {
        XSetForeground(dpy, backgc, 0x6c7086);
        if (font) XSetFont(dpy, backgc, font->fid);
        XDrawString(dpy, backbuf, backgc, x + 6, y + h/2 + 5, placeholder, strlen(placeholder));
    }
    // click to focus
    if (click_x >= x && click_x < x+w && click_y >= y && click_y < y+h) {
        input_focus = idx;
    }
    // copy to user buffer
    if (buf && bufsize > 0) {
        int copylen = input_len[idx] < bufsize - 1 ? input_len[idx] : bufsize - 1;
        memcpy(buf, input_bufs[idx], copylen);
        buf[copylen] = '\0';
    }
    return (input_focus == idx) ? 1 : 0;
}

int ketiwe_key_press(void) {
    int k = last_key;
    last_key = 0;
    return k;
}

const char *ketiwe_input_text(int index) {
    if (index < 0 || index >= 8) return "";
    return input_bufs[index];
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
            last_key = (int)ks;
            key_pressed = 1;
            // text input handling
            if (input_focus >= 0 && input_focus < 8) {
                char kbuf[8];
                int klen = XLookupString(&ev.xkey, kbuf, sizeof(kbuf), NULL, NULL);
                if (ks == XK_BackSpace) {
                    if (input_len[input_focus] > 0) input_len[input_focus]--;
                } else if (ks == XK_Return || ks == XK_Tab) {
                    input_focus = -1;
                } else if (klen == 1 && kbuf[0] >= 32 && kbuf[0] < 127) {
                    if (input_len[input_focus] < 255) {
                        input_bufs[input_focus][input_len[input_focus]++] = kbuf[0];
                        input_bufs[input_focus][input_len[input_focus]] = '\0';
                    }
                }
            }
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

// ── Sprites: PPM (P3 text / P6 binary) load, X11 blit ───────────────
// No image-format deps: convert PNG/JPG to PPM once with
// `convert in.png out.ppm` or `ffmpeg -i in.png out.ppm`.
#define KETIWE_MAX_SPRITES 64
#define KETIWE_MAX_SPR_DIM 512
typedef struct { int used; int w; int h; unsigned char *px; } KetiweSprite;
static KetiweSprite ketiwe_sprites[KETIWE_MAX_SPRITES];

static int ketiwe_ppm_next_ascii(FILE *f, int *v) {
    int c;
    for (;;) {
        c = fgetc(f);
        if (c == EOF) return 0;
        if (c == '#') { while ((c = fgetc(f)) != EOF && c != '\n'); continue; }
        if (c==' '||c=='\t'||c=='\r'||c=='\n') continue;
        break;
    }
    ungetc(c, f);
    return fscanf(f, "%d", v) == 1;
}

int ketiwe_sprite_load(const char *path) {
    if (!path) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    int slot = -1;
    for (int i = 0; i < KETIWE_MAX_SPRITES; i++) {
        if (!ketiwe_sprites[i].used) { slot = i; break; }
    }
    if (slot < 0) { fclose(f); return -1; }
    char magic[2];
    if (fread(magic, 1, 2, f) != 2 || magic[0] != 'P' ||
        (magic[1] != '3' && magic[1] != '6')) { fclose(f); return -1; }
    int ascii = (magic[1] == '3');
    int w = 0, h = 0, maxv = 0;
    if (ascii) {
        if (!ketiwe_ppm_next_ascii(f, &w) || !ketiwe_ppm_next_ascii(f, &h) ||
            !ketiwe_ppm_next_ascii(f, &maxv)) { fclose(f); return -1; }
    } else {
        // binary header: same tokens, then exactly one whitespace byte
        char hdr[64]; int hn = 0, vals[3], nv = 0;
        int c, in_comment = 0;
        while (nv < 3 && (c = fgetc(f)) != EOF) {
            if (in_comment) { if (c == '\n') in_comment = 0; continue; }
            if (c == '#') { in_comment = 1; continue; }
            if (c==' '||c=='\t'||c=='\r'||c=='\n') {
                if (hn > 0) { hdr[hn] = '\0'; vals[nv++] = atoi(hdr); hn = 0; }
                continue;
            }
            if (hn < (int)sizeof(hdr)-1) hdr[hn++] = (char)c;
        }
        if (nv < 3) { fclose(f); return -1; }
        w = vals[0]; h = vals[1]; maxv = vals[2];
        // nv==3 means we stopped at the single delimiter right after maxv
    }
    if (w <= 0 || h <= 0 || w > KETIWE_MAX_SPR_DIM || h > KETIWE_MAX_SPR_DIM ||
        maxv <= 0 || maxv > 255) { fclose(f); return -1; }
    unsigned char *px = malloc((size_t)w * h * 3);
    if (!px) { fclose(f); return -1; }
    for (int i = 0; i < w * h * 3; i++) {
        int v = 0;
        if (ascii) {
            if (!ketiwe_ppm_next_ascii(f, &v)) { free(px); fclose(f); return -1; }
        } else {
            v = fgetc(f);
            if (v == EOF) { free(px); fclose(f); return -1; }
        }
        if (v < 0) v = 0;
        if (v > maxv) v = maxv;
        px[i] = (unsigned char)(v * 255 / maxv);
    }
    fclose(f);
    ketiwe_sprites[slot].used = 1;
    ketiwe_sprites[slot].w = w;
    ketiwe_sprites[slot].h = h;
    ketiwe_sprites[slot].px = px;
    return slot;
}

static XImage *ketiwe_sprite_image(int w, int h) {
    if (!dpy || w <= 0 || h <= 0) return NULL;
    int scr = DefaultScreen(dpy);
    XImage *img = XCreateImage(dpy, DefaultVisual(dpy, scr), DefaultDepth(dpy, scr),
                               ZPixmap, 0, NULL, w, h, 8, 0);
    if (!img) return NULL;
    img->data = malloc(img->bytes_per_line * (size_t)h);
    if (!img->data) { img->data = NULL; XDestroyImage(img); return NULL; }
    return img;
}

static unsigned long ketiwe_sprite_pixel(unsigned char *p) {
    return ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2];
}

static int ketiwe_sprite_ok(int id) {
    return id >= 0 && id < KETIWE_MAX_SPRITES && ketiwe_sprites[id].used;
}

void ketiwe_sprite_draw(int id, int x, int y) {
    if (!dpy || !win || !backbuf) return;
    if (!ketiwe_sprite_ok(id)) return;
    int w = ketiwe_sprites[id].w, h = ketiwe_sprites[id].h;
    XImage *img = ketiwe_sprite_image(w, h);
    if (!img) return;
    for (int r = 0; r < h; r++) {
        for (int c = 0; c < w; c++) {
            unsigned char *p = &ketiwe_sprites[id].px[(size_t)(r * w + c) * 3];
            XPutPixel(img, c, r, ketiwe_sprite_pixel(p));
        }
    }
    XPutImage(dpy, backbuf, backgc, img, 0, 0, x, y, w, h);
    XDestroyImage(img);
}

void ketiwe_sprite_draw_key(int id, int x, int y, unsigned key) {
    if (!dpy || !win || !backbuf) return;
    if (!ketiwe_sprite_ok(id)) return;
    key &= 0xFFFFFF;
    int w = ketiwe_sprites[id].w, h = ketiwe_sprites[id].h;
    // transparent blit: draw opaque runs directly onto the back buffer
    for (int r = 0; r < h; r++) {
        int run = -1;
        for (int c = 0; c <= w; c++) {
            unsigned long v = 0;
            int opaque = 0;
            if (c < w) {
                unsigned char *p = &ketiwe_sprites[id].px[(size_t)(r * w + c) * 3];
                v = ketiwe_sprite_pixel(p);
                opaque = (v != key);
            }
            if (opaque && run < 0) run = c;
            if (!opaque && run >= 0) {
                XImage *img = ketiwe_sprite_image(c - run, 1);
                if (img) {
                    for (int k = run; k < c; k++) {
                        unsigned char *p = &ketiwe_sprites[id].px[(size_t)(r * w + k) * 3];
                        XPutPixel(img, k - run, 0, ketiwe_sprite_pixel(p));
                    }
                    XPutImage(dpy, backbuf, backgc, img, 0, 0, x + run, y + r, c - run, 1);
                    XDestroyImage(img);
                }
                run = -1;
            }
        }
    }
}

void ketiwe_sprite_draw_scaled(int id, int x, int y, int w, int h) {
    if (!dpy || !win || !backbuf) return;
    if (!ketiwe_sprite_ok(id)) return;
    if (w <= 0 || h <= 0 || w > KETIWE_MAX_SPR_DIM * 2 || h > KETIWE_MAX_SPR_DIM * 2) return;
    int sw = ketiwe_sprites[id].w, sh = ketiwe_sprites[id].h;
    XImage *img = ketiwe_sprite_image(w, h);
    if (!img) return;
    for (int r = 0; r < h; r++) {
        int sr = r * sh / h;
        for (int c = 0; c < w; c++) {
            int sc = c * sw / w;
            unsigned char *p = &ketiwe_sprites[id].px[(size_t)(sr * sw + sc) * 3];
            XPutPixel(img, c, r, ketiwe_sprite_pixel(p));
        }
    }
    XPutImage(dpy, backbuf, backgc, img, 0, 0, x, y, w, h);
    XDestroyImage(img);
}

void ketiwe_sprite_draw_region(int id, int sx, int sy, int sw, int sh, int dx, int dy) {    if (!dpy || !win || !backbuf) return;
    if (!ketiwe_sprite_ok(id)) return;
    int iw = ketiwe_sprites[id].w, ih = ketiwe_sprites[id].h;
    // clip source rect to the sprite
    if (sx < 0) { sw += sx; dx -= sx; sx = 0; }
    if (sy < 0) { sh += sy; dy -= sy; sy = 0; }
    if (sx + sw > iw) sw = iw - sx;
    if (sy + sh > ih) sh = ih - sy;
    if (sw <= 0 || sh <= 0) return;
    XImage *img = ketiwe_sprite_image(sw, sh);
    if (!img) return;
    for (int r = 0; r < sh; r++) {
        for (int c = 0; c < sw; c++) {
            unsigned char *p = &ketiwe_sprites[id].px[(size_t)((sy + r) * iw + (sx + c)) * 3];
            XPutPixel(img, c, r, ketiwe_sprite_pixel(p));
        }
    }
    XPutImage(dpy, backbuf, backgc, img, 0, 0, dx, dy, sw, sh);
    XDestroyImage(img);
}

void ketiwe_sprite_draw_region_key(int id, int sx, int sy, int sw, int sh,
                                   int dx, int dy, unsigned key) {
    if (!dpy || !win || !backbuf) return;
    if (!ketiwe_sprite_ok(id)) return;
    key &= 0xFFFFFF;
    int iw = ketiwe_sprites[id].w, ih = ketiwe_sprites[id].h;
    if (sx < 0) { sw += sx; dx -= sx; sx = 0; }
    if (sy < 0) { sh += sy; dy -= sy; sy = 0; }
    if (sx + sw > iw) sw = iw - sx;
    if (sy + sh > ih) sh = ih - sy;
    if (sw <= 0 || sh <= 0) return;
    // transparent region blit: opaque runs per row, like draw_key
    for (int r = 0; r < sh; r++) {
        int run = -1;
        for (int c = 0; c <= sw; c++) {
            unsigned long v = 0;
            int opaque = 0;
            if (c < sw) {
                unsigned char *p = &ketiwe_sprites[id].px[(size_t)((sy + r) * iw + (sx + c)) * 3];
                v = ketiwe_sprite_pixel(p);
                opaque = (v != key);
            }
            if (opaque && run < 0) run = c;
            if (!opaque && run >= 0) {
                XImage *img = ketiwe_sprite_image(c - run, 1);
                if (img) {
                    for (int k = run; k < c; k++) {
                        unsigned char *p = &ketiwe_sprites[id].px[(size_t)((sy + r) * iw + (sx + k)) * 3];
                        XPutPixel(img, k - run, 0, ketiwe_sprite_pixel(p));
                    }
                    XPutImage(dpy, backbuf, backgc, img, 0, 0, dx + run, dy + r, c - run, 1);
                    XDestroyImage(img);
                }
                run = -1;
            }
        }
    }
}

int ketiwe_sprite_w(int id) {
    if (id < 0 || id >= KETIWE_MAX_SPRITES || !ketiwe_sprites[id].used) return -1;
    return ketiwe_sprites[id].w;
}

int ketiwe_sprite_h(int id) {
    if (id < 0 || id >= KETIWE_MAX_SPRITES || !ketiwe_sprites[id].used) return -1;
    return ketiwe_sprites[id].h;
}

void ketiwe_sprite_free(int id) {
    if (id < 0 || id >= KETIWE_MAX_SPRITES || !ketiwe_sprites[id].used) return;
    free(ketiwe_sprites[id].px);
    ketiwe_sprites[id].px = NULL;
    ketiwe_sprites[id].used = 0;
}
#else
int ketiwe_init(void){return 0;}
int ketiwe_window(const char *t,int w,int h){(void)t;(void)w;(void)h;return 0;}
void ketiwe_rect(int x,int y,int w,int h,unsigned c){(void)x;(void)y;(void)w;(void)h;(void)c;}
void ketiwe_rect_outline(int x,int y,int w,int h,unsigned c){(void)x;(void)y;(void)w;(void)h;(void)c;}
void ketiwe_line(int x1,int y1,int x2,int y2,unsigned c){(void)x1;(void)y1;(void)x2;(void)y2;(void)c;}
void ketiwe_clear(unsigned c){(void)c;}
void ketiwe_circle(int cx,int cy,int r,unsigned c){(void)cx;(void)cy;(void)r;(void)c;}
void ketiwe_text(int x,int y,const char *t){(void)x;(void)y;(void)t;}
int ketiwe_button(int x,int y,int w,int h,const char *l){(void)x;(void)y;(void)w;(void)h;(void)l;return 0;}
int ketiwe_input(int x,int y,int w,int h,char *b,int bs,const char *ph){(void)x;(void)y;(void)w;(void)h;(void)b;(void)bs;(void)ph;return 0;}
int ketiwe_poll(void){return 1;}
void ketiwe_flip(void){}
void ketiwe_close(void){}
int ketiwe_key_press(void){return 0;}
const char *ketiwe_input_text(int i){(void)i;return "";}
int ketiwe_sprite_load(const char *p){(void)p;return -1;}
void ketiwe_sprite_draw(int id,int x,int y){(void)id;(void)x;(void)y;}
void ketiwe_sprite_draw_key(int id,int x,int y,unsigned k){(void)id;(void)x;(void)y;(void)k;}
void ketiwe_sprite_draw_scaled(int id,int x,int y,int w,int h){(void)id;(void)x;(void)y;(void)w;(void)h;}
void ketiwe_sprite_draw_region(int id,int sx,int sy,int sw,int sh,int dx,int dy){(void)id;(void)sx;(void)sy;(void)sw;(void)sh;(void)dx;(void)dy;}
void ketiwe_sprite_draw_region_key(int id,int sx,int sy,int sw,int sh,int dx,int dy,unsigned k){(void)id;(void)sx;(void)sy;(void)sw;(void)sh;(void)dx;(void)dy;(void)k;}
int ketiwe_sprite_w(int id){(void)id;return -1;}
int ketiwe_sprite_h(int id){(void)id;return -1;}
void ketiwe_sprite_free(int id){(void)id;}
#endif
