#include "commonapi.h"
#include <stdlib.h>
#include <string.h>

void onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    free(window->title);
    free(window->content);
  }
  if(event == WINDOW_EVENT_RESIZE)
    window->content = realloc(window->content, window->w * window->h * sizeof(short));
}
void update(window_t* window, desktop_t* desktop) {
  (void)desktop;
  for(int j = 0; j < window->h; ++j) {
    for(int i = 0; i < window->w; ++i) {
      short c = window->content[j * window->w + i];
      short attr = c >> 8;
      attr = (attr + 1) % 256;
      window->content[j * window->w + i] = (c & 255) | (attr << 8);
    }
  }
}
void draw(window_t* window, desktop_t* desktop) {
  (void)desktop;
  (void)window;
}

void window_init(window_t* win) {
  win->x = 0;
  win->y = 0;
  win->w = 20;
  win->h = 10;
  win->title = strdup("example app");
  win->content = calloc(win->w * win->h, sizeof(short));
  for(int i = 0; i < win->w * win->h; ++i) win->content[i] = 'A' | (0b00001010 << 8);
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
}
