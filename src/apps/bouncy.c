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

// should i even do that for example apps

#include "commonapi.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
  int bx, by, bvx, bvy;
  int fskip;
  int self_index;
} bouncy_state_t;
bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  (void)data;
  bouncy_state_t* s = window->data;
  if(event == WINDOW_EVENT_CLOSE) {
    free(window->title);
    free(window->content);
    free(s);
    window->data = NULL;
    return false;
  }
  if(event == WINDOW_EVENT_RESIZE) {
    short* tmp = realloc(window->content, window->w * window->h * sizeof(short));
    if(!tmp) {
      window->close_pending = true;
      return false;
    }
    window->content = tmp;
    if(s->bx >= window->w) s->bx = window->w - 1;
    if(s->by >= window->h) s->by = window->h - 1;
  }
  if(event == WINDOW_EVENT_FOCUS) return true;
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)desktop;
  if(!window->content) return;
  bouncy_state_t* s = window->data;
  s->fskip = (s->fskip + 1) % 4;
  if(s->fskip) return;
  int nbx = s->bx + s->bvx;
  int nby = s->by + s->bvy;
  if(nbx < 0) {
    nbx = -nbx;
    s->bvx = abs(s->bvx);
  } else if(nbx >= window->w) {
    nbx = 2 * (window->w - 1) - nbx;
    s->bvx = -abs(s->bvx);
  }
  if(nby < 0) {
    nby = -nby;
    s->bvy = abs(s->bvy);
  } else if(nby >= window->h) {
    nby = 2 * (window->h - 1) - nby;
    s->bvy = -abs(s->bvy);
  }
  s->bx = nbx;
  s->by = nby;
}
void draw(window_t* window, desktop_t* desktop) {
  (void)desktop;
  if(!window->content) return;
  bouncy_state_t* s = window->data;
  for(int i = 0; i < window->w * window->h; ++i) window->content[i] = ' ' | (0b01110000 << 8);
  window->content[s->by * window->w + s->bx] = 'O' | (0b00001010 << 8);
}

void window_init(desktop_t* desktop, window_t* win) {
  (void)desktop;
  bouncy_state_t* s = calloc(1, sizeof(bouncy_state_t));
  s->bvx = 1;
  s->bvy = 1;
  win->x = 0;
  win->y = 0;
  win->w = 20;
  win->h = 10;
  win->title = strdup("bouncy ball?");
  win->content = calloc(win->w * win->h, sizeof(short));
  for(int i = 0; i < win->w * win->h; ++i) win->content[i] = ' ' | (0b01110000 << 8);
  win->data = s;
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
}
