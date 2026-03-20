#include "commonapi.h"
#include "tw.h"
#include <stdlib.h>
#include <string.h>

static void shadow_drawer(desktop_t* desktop, window_t* window, int i) {
  (void)desktop;
  (void)i;
  tw_fill(window->x + window->w, window->y + 1, 1, window->h + 1, 0);
  tw_fill(window->x + 1, window->y + window->h + 1, window->w, 1, 0);
}
bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    if(!window->hidden) {
      window->hidden = true;
      return true;
    }
    if(desktop->after_window_draw == shadow_drawer) desktop->after_window_draw = NULL;
    free(window->title);
    free(window->content);
    return false;
  }
  if(event == WINDOW_EVENT_CLOSE_FORCE) {
    if(desktop->after_window_draw == shadow_drawer) desktop->after_window_draw = NULL;
    free(window->title);
    free(window->content);
    return false;
  }
  if(event == WINDOW_EVENT_FOCUS) {
    window->hidden = false;
    return false;
  }
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)window;
  if(desktop->after_window_draw != shadow_drawer) desktop->after_window_draw = shadow_drawer;
}
void draw(window_t* window, desktop_t* desktop) {
  (void)desktop;
  (void)window;
}

void window_init(window_t* win) {
  win->x = 2;
  win->y = 2;
  win->w = 18;
  win->h = 3;
  win->unresizable = true;
  win->title = strdup("Shadows");
  win->content = calloc(win->w * win->h, sizeof(short));
  for(int i = 0; i < win->w * win->h; ++i) win->content[i] = ' ' | (0b01110000 << 8);
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
}
