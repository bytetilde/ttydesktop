#include "commonapi.h"
#include "tw.h"
#include <string.h>

static void shadow_drawer(desktop_t* desktop, window_t* window, int i) {
  (void)desktop;
  (void)i;
  tw_fill(window->x + window->w, window->y + 1, 1, window->h + 1, 0);
  tw_fill(window->x + 1, window->y + window->h + 1, window->w, 1, 0);
}
bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)window;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    if(desktop->after_window_draw == shadow_drawer) desktop->after_window_draw = NULL;
    return false;
  }
  if(event == WINDOW_EVENT_CLOSE_FORCE) {
    if(desktop->after_window_draw == shadow_drawer) desktop->after_window_draw = NULL;
    return false;
  }
  if(event == WINDOW_EVENT_FOCUS) return true;
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)window;
  if(desktop->after_window_draw != shadow_drawer) desktop->after_window_draw = shadow_drawer;
}
void window_init(window_t* win) {
  win->hidden = true;
  win->update = update;
  win->onevent = onevent;
}
