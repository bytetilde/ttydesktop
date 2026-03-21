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
#include "hookman.h"
#include "tw.h"
#include <string.h>

static hookman_t* hm;

static void* shadow_drawer(hook_payload_t* payload) {
  if(!payload || !payload->window) return NULL;
  tw_fill(payload->window->x + payload->window->w, payload->window->y + 1, 1,
          payload->window->h + 1, 0);
  tw_fill(payload->window->x + 1, payload->window->y + payload->window->h + 1, payload->window->w,
          1, 0);
  return NULL;
}
bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)window;
  (void)desktop;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    hm = hookman_find(desktop);
    if(hm) hookman_detach_all(hm, "shadows");
    return false;
  }
  if(event == WINDOW_EVENT_FOCUS) return true;
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)window;
  (void)desktop;
}
void window_init(desktop_t* desktop, window_t* win) {
  win->hidden = true;
  hm = hookman_find(desktop);
  if(!hm) {
    win->close_pending = true;
    return;
  }
  hookman_attach_after(hm, "shadows", "desktop_window_draw", shadow_drawer);
  win->update = update;
  win->onevent = onevent;
}
