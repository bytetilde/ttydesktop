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

#pragma once
#include <stdbool.h>

typedef struct desktop_t desktop_t;
typedef struct window_t window_t;
typedef enum window_event_t {
  WINDOW_EVENT_NONE,
  WINDOW_EVENT_KEY,
  WINDOW_EVENT_MOVE,
  WINDOW_EVENT_RESIZE,
  WINDOW_EVENT_FOCUS,
  WINDOW_EVENT_UNFOCUS,
  WINDOW_EVENT_CLOSE,
  WINDOW_EVENT_OPEN,
  WINDOW_EVENT_CLOSE_FORCE,
} window_event_t;
typedef struct window_move_event_t {
  int dx, dy;
} window_move_event_t;
typedef struct window_resize_event_t {
  int dw, dh;
} window_resize_event_t;
typedef int window_key_event_t;
typedef struct window_t {
  int x, y, w, h;
  char* title;
  short* content;
  bool hidden;
  bool unmovable;
  bool unresizable;
  bool close_pending;
  void (*update)(window_t* window, desktop_t* desktop);
  void (*draw)(window_t* window, desktop_t* desktop);
  bool (*onevent)(window_t* window, desktop_t* desktop, int event, void* data);
  void* handle;
  void* data;
} window_t;
typedef enum desktop_state_t {
  DESKTOP_STATE_NORMAL,
  DESKTOP_STATE_PROMPT_FOCUS,
  DESKTOP_STATE_PROMPT_MOVE,
  DESKTOP_STATE_PROMPT_RESIZE,
  DESKTOP_STATE_PROMPT_CLOSE,
  DESKTOP_STATE_PROMPT_OPEN,
  DESKTOP_STATE_FOCUSED,
  DESKTOP_STATE_MOVING,
  DESKTOP_STATE_RESIZING
} desktop_state_t;
typedef struct desktop_t {
  window_t* windows;
  int window_count;
  int window_capacity;
  char* statustext;
  desktop_state_t state;
  char buf[256];
  int buflen;
  int cursor_pos;
  int target;
  int ox, oy, ow, oh;
  double statustimer;
  bool (*update)(desktop_t* desktop);
  void (*draw)(desktop_t* desktop);
  bool (*flush)(desktop_t* desktop);
  bool (*onkey)(desktop_t* desktop, int key);
  void (*close_window)(desktop_t* desktop, int index);
  bool (*dispatch_window_event)(desktop_t* desktop, window_t* window, int event, void* data);
} desktop_t;
