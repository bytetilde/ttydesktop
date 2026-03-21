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
#include <time.h>
static hookman_t* hm;

static void* clock_drawer(hook_payload_t* payload) {
  if(!payload || !payload->desktop) return NULL;
  time_t now = time(NULL);
  struct tm* tm_info = localtime(&now);
  char buf[32];
  strftime(buf, sizeof(buf), "%H:%M:%S", tm_info);
  tw_wh_t size = tw_get_size();
  int len = strlen(buf);
  tw_puts(buf, size.w - len, size.h - 1, 0b01110000);
  return NULL;
}

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)window;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    hm = hookman_find(desktop);
    if(hm) hookman_detach_all(hm, "clock");
    if(window->title) free(window->title);
    window->title = NULL;
    return false;
  }
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
  hookman_attach_after(hm, "clock", "desktop_status_draw", clock_drawer);
  win->update = update;
  win->onevent = onevent;
  win->title = strdup("clock");
}
