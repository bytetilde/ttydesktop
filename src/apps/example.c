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
#include <stdlib.h>
#include <string.h>

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    free(window->title);
    free(window->content);
  }
  if(event == WINDOW_EVENT_RESIZE) {
    short* tmp = realloc(window->content, window->w * window->h * sizeof(short));
    if(!tmp) {
      free(window->content);
      window->content = NULL;
      return false;
    }
    window->content = tmp;
    memset(window->content, 0, window->w * window->h * sizeof(short));
  }
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)window;
  (void)desktop;
}
void draw(window_t* window, desktop_t* desktop) {
  (void)window;
  (void)desktop;
}

void window_init(desktop_t* desktop, window_t* win) {
  (void)desktop;
  win->x = 0;
  win->y = 0;
  win->w = 20;
  win->h = 10;
  win->title = strdup("example");
  win->content = calloc(win->w * win->h, sizeof(short));
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
}
