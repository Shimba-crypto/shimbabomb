#ifndef SB_KETIWE_H
#define SB_KETIWE_H
// Ketiwe GUI — from-scratch X11 pixel toolkit, no GTK/WebKit
int ketiwe_init(void);
int ketiwe_window(const char *title, int w, int h);
void ketiwe_rect(int x, int y, int w, int h, unsigned color);
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
#endif
