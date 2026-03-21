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

static hookman_t* hm;
typedef struct frames_state_t {
  int fps;
  bool show_frames;
  unsigned long long starttime;
} frames_state_t;
static frames_state_t* fs;

void* before_desktop_update(hook_payload_t* payload) {
  if(!payload || !payload->desktop) return NULL;
  if(!fs) return NULL;
  return NULL;
}
void* after_desktop_draw(hook_payload_t* payload) {
  if(!payload || !payload->desktop) return NULL;
  if(!fs || !fs->show_frames) return NULL;
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  unsigned long long now = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  unsigned long long elapsed = now - fs->starttime;
  unsigned long long elapsedus = elapsed / 1000;
  char buf[256];
  snprintf(buf, sizeof(buf), "%lluus / %llu", elapsedus, 1000000ULL / fs->fps);
  tw_printf(tw_w - strlen(buf), 0, 0b00001111, buf);
  double fps = 1000000000.0 / elapsed;
  if(fps > fs->fps) fps = fs->fps;
  snprintf(buf, sizeof(buf), "fps: %.2f", fps);
  tw_printf(tw_w - strlen(buf), 1, 0b00001111, buf);
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
  unsigned long long elapsed = now - fs->starttime;
  if(elapsed < tns) {
    struct timespec rem;
    struct timespec req = {.tv_sec = 0, .tv_nsec = tns - elapsed};
    nanosleep(&req, &rem);
  }
  clock_gettime(CLOCK_MONOTONIC, &ts);
  now = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  fs->starttime = now;
  return (void*)1;
}

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    hm = hookman_find(desktop);
    if(hm) hookman_detach_all(hm, "frames");
    if(window->title) {
      free(window->title);
      window->title = NULL;
    }
    free(window->data);
    window->data = NULL;
    fs = NULL;
  }
  if(event == WINDOW_EVENT_FOCUS) {
    ((frames_state_t*)window->data)->show_frames ^= true;
    return true;
  }
  if(event == WINDOW_EVENT_RESIZE) return true;
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
  win->hidden = true;
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
  fs->fps = 60; // xd
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  fs->starttime = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
  win->title = strdup("frames");
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
  win->data = fs;
}
