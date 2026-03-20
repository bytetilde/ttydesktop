#include "commonapi.h"
#include <stdlib.h>
#include <string.h>
static int bx = 0, by = 0, bvx = 1, bvy = 1;
static int fskip = 0;

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    free(window->title);
    free(window->content);
  }
  if(event == WINDOW_EVENT_RESIZE) {
    window->content = realloc(window->content, window->w * window->h * sizeof(short));
    if(bx >= window->w) bx = window->w - 1;
    if(by >= window->h) by = window->h - 1;
  }
  if(event == WINDOW_EVENT_FOCUS) return true;
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  fskip = (fskip + 1) % 4;
  if(fskip) return;
  (void)desktop;
  if(bx + bvx < 0 || bx + bvx >= window->w) bvx = -bvx;
  if(by + bvy < 0 || by + bvy >= window->h) bvy = -bvy;
  bx += bvx;
  by += bvy;
}
void draw(window_t* window, desktop_t* desktop) {
  (void)desktop;
  for(int i = 0; i < window->w * window->h; ++i) window->content[i] = ' ' | (0b01110000 << 8);
  window->content[by * window->w + bx] = 'O' | (0b00001010 << 8);
}

void window_init(window_t* win) {
  win->x = 0;
  win->y = 0;
  win->w = 20;
  win->h = 10;
  win->title = strdup("bouncy ball?");
  win->content = calloc(win->w * win->h, sizeof(short));
  for(int i = 0; i < win->w * win->h; ++i) win->content[i] = ' ' | (0b01110000 << 8);
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
}
