/**
 * Copyright (C) 2026 bytetilde
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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
