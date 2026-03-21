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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

static hookman_t* hm;
typedef struct frames_state_t {
  int fps;
  bool show_frames;
  unsigned long long start_time;
  double last_fps;
  unsigned long long last_elapsed_us;
} frames_state_t;
static frames_state_t* fs;

void* before_desktop_update(hook_payload_t* payload) {
  if(!payload || !payload->desktop) return NULL;
  if(!fs) return NULL;
  return NULL;
}
void* after_desktop_draw(hook_payload_t* payload) {
  if(!payload || !payload->desktop) return NULL;
  if(!fs) return NULL;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  unsigned long long now = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  unsigned long long elapsed = now - fs->start_time;
  if(elapsed == 0) elapsed = 1;
  fs->last_elapsed_us = elapsed / 1000;
  fs->last_fps = 1000000000.0 / elapsed;
  if(fs->last_fps > 1000) fs->last_fps = 1000;
  return NULL;
}
void* desktop_flush(hook_payload_t* payload) {
  if(!payload || !payload->desktop) return NULL;
  if(!fs) return NULL;
  tw_flush();
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  unsigned long long now = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  unsigned long long tns = 1000000000ULL / fs->fps;
  unsigned long long elapsed = now - fs->start_time;
  if(elapsed < tns) {
    struct timespec rem;
    struct timespec req = {.tv_sec = 0, .tv_nsec = tns - elapsed};
    nanosleep(&req, &rem);
  }
  clock_gettime(CLOCK_MONOTONIC, &ts);
  now = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  fs->start_time = now;
  return (void*)1;
}

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  if(event == WINDOW_EVENT_CLOSE) {
    if(!window->hidden) {
      window->hidden = true;
      return true;
    }
  }
  if(event == WINDOW_EVENT_CLOSE || event == WINDOW_EVENT_CLOSE_FORCE) {
    hm = hookman_find(desktop);
    if(hm) hookman_detach_all(hm, "frames");
    if(window->title) {
      free(window->title);
      window->title = NULL;
    }
    if(window->content) {
      free(window->content);
      window->content = NULL;
    }
    free(window->data);
    window->data = NULL;
    fs = NULL;
    return false;
  }
  if(event == WINDOW_EVENT_KEY) {
    int key = (int)(long)data;
    if(key == '+' || key == '=') fs->fps = MIN(fs->fps + 5, 240);
    else if(key == '-' || key == '_') fs->fps = MAX(fs->fps - 5, 1);
    else if(key == 'e') fs->fps = MIN(fs->fps + 1, 240);
    else if(key == 'q') fs->fps = MAX(fs->fps - 1, 1);
  }
  if(event == WINDOW_EVENT_RESIZE) return true;
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)window;
  (void)desktop;
}
void draw(window_t* window, desktop_t* desktop) {
  (void)desktop;
  if(!window->content) {
    window->w = 25;
    window->h = 2;
    window->content = malloc(window->w * window->h * sizeof(short));
  }
  char buf[64];
  snprintf(buf, sizeof(buf), "fps: %.2f (tgt: %d)", fs->last_fps, fs->fps);
  for(int i = 0; i < window->w; i++) {
    char c = (i < (int)strlen(buf)) ? buf[i] : ' ';
    window->content[i] = (0x0F << 8) | c;
  }
  snprintf(buf, sizeof(buf), "%llu/%lluus", fs->last_elapsed_us, 1000000ULL / fs->fps);
  for(int i = 0; i < window->w; i++) {
    char c = (i < (int)strlen(buf)) ? buf[i] : ' ';
    window->content[window->w + i] = (0x07 << 8) | c;
  }
}

void window_init(desktop_t* desktop, window_t* win) {
  (void)desktop;
  if(fs) {
    win->close_pending = true;
    return;
  }
  hm = hookman_find(desktop);
  if(!hm) {
    win->close_pending = true;
    return;
  }
  hookman_attach_before(hm, "frames", "desktop_update", before_desktop_update);
  hookman_attach_after(hm, "frames", "desktop_draw", after_desktop_draw);
  hookman_attach(hm, "frames", "desktop_flush", desktop_flush);
  fs = calloc(1, sizeof(frames_state_t));
  fs->fps = 60;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  fs->start_time = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  win->title = strdup("frames");
  win->w = 28;
  win->h = 2;
  win->unresizable = true;
  win->x = tw_w - win->w;
  win->y = 0;
  win->content = malloc(win->w * win->h * sizeof(short));
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
  win->data = fs;
}
