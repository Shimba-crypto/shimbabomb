#ifndef SB_KETIWE_H
#define SB_KETIWE_H
// Ketiwe GUI — from-scratch X11 pixel toolkit, no GTK/WebKit
int ketiwe_init(void);
int ketiwe_window(const char *title, int w, int h);
void ketiwe_rect(int x, int y, int w, int h, unsigned color);
void ketiwe_rect_outline(int x, int y, int w, int h, unsigned color);
void ketiwe_line(int x1, int y1, int x2, int y2, unsigned color);
void ketiwe_clear(unsigned color);
void ketiwe_circle(int cx, int cy, int r, unsigned color);
void ketiwe_text(int x, int y, const char *text);
int ketiwe_button(int x, int y, int w, int h, const char *label);
int ketiwe_input(int x, int y, int w, int h, char *buf, int bufsize, const char *placeholder);
int ketiwe_poll(void);
void ketiwe_flip(void);
void ketiwe_close(void);
int ketiwe_mouse_x(void);
int ketiwe_mouse_y(void);
int ketiwe_mouse_down(void);
int ketiwe_key_press(void);
const char *ketiwe_input_text(int index);
int ketiwe_sprite_load(const char *path);
void ketiwe_sprite_draw(int id, int x, int y);
void ketiwe_sprite_draw_key(int id, int x, int y, unsigned key);
void ketiwe_sprite_draw_scaled(int id, int x, int y, int w, int h);
void ketiwe_sprite_draw_region(int id, int sx, int sy, int sw, int sh, int dx, int dy);
void ketiwe_sprite_draw_region_key(int id, int sx, int sy, int sw, int sh, int dx, int dy, unsigned key);
int ketiwe_sprite_w(int id);
int ketiwe_sprite_h(int id);
void ketiwe_sprite_free(int id);
#endif
