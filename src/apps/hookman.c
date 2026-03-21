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

#include "hookman.h"
#include "commonapi.h"
#include "tw.h"
#include <dlfcn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static hookman_t* hookman = NULL;

bool call_hooks(hook_payload_t* payload, const char* hook_point) {
  unsigned long long h = hash(hook_point);
  pthread_mutex_lock(&hookman->lock);
  hook_t* hooks = hm_get(hookman->hooks, h);
  pthread_mutex_unlock(&hookman->lock);
  if(!hooks) return false;
  while(hooks) {
    if(hooks->function(payload)) return true;
    hooks = hooks->next;
  }
  return false;
}
bool call_hooks_before(hook_payload_t* payload, const char* hook_point) {
  unsigned long long h = hash(hook_point);
  pthread_mutex_lock(&hookman->lock);
  hook_t* hooks = hm_get(hookman->hooks_before, h);
  pthread_mutex_unlock(&hookman->lock);
  if(!hooks) return false;
  while(hooks) {
    if(hooks->function(payload)) return true;
    hooks = hooks->next;
  }
  return false;
}
void call_hooks_after(hook_payload_t* payload, const char* hook_point) {
  unsigned long long h = hash(hook_point);
  pthread_mutex_lock(&hookman->lock);
  hook_t* hooks = hm_get(hookman->hooks_after, h);
  pthread_mutex_unlock(&hookman->lock);
  while(hooks) {
    hooks->function(payload);
    hooks = hooks->next;
  }
}

typedef struct window_thread_arg_t {
  window_t* window;
  desktop_t* desktop;
} window_thread_arg_t;
static void set_status(desktop_t* desktop, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(desktop->statustext, 256, fmt, args);
  va_end(args);
}
static void* window_update_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  if(warg->window->update) warg->window->update(warg->window, warg->desktop);
  return NULL;
}
static void* window_draw_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  desktop_t* desktop = warg->desktop;
  window_t* window = warg->window;
  if(window->hidden) return NULL;
  hook_payload_t payload = {.desktop = desktop, .window = window, .data = NULL};
  if(call_hooks_before(&payload, "window_draw")) return NULL;
  unsigned long long h = hash("window_draw");
  pthread_mutex_lock(&hookman->lock);
  bool has_override = hm_get(hookman->hooks, h) != NULL;
  pthread_mutex_unlock(&hookman->lock);
  if(has_override) call_hooks(&payload, "window_draw");
  else if(window->draw) window->draw(window, desktop);
  call_hooks_after(&payload, "window_draw");
  return NULL;
}
static bool desktop_update(desktop_t* desktop) {
  hook_payload_t payload = {.desktop = desktop, .window = NULL, .data = NULL};
  if(call_hooks_before(&payload, "desktop_update")) return false;
  int ch = tw_getch();
  if(ch != -1 && ch != 0) {
    hook_payload_t kpayload = {.desktop = desktop, .window = NULL, .data = (void*)(long)ch};
    if(call_hooks_before(&kpayload, "key")) goto skip_key;
    if(desktop->onkey) {
      if(desktop->onkey(desktop, ch)) goto skip_key;
    } else if(desktop->state == STATE_NORMAL) {
      if(ch == 'q') return true;
      else if(ch == 'f' || ch == 'm' || ch == 'r' || ch == 'c' || ch == 'o') {
        desktop->buflen = 0;
        desktop->buf[0] = '\0';
        if(ch == 'f') {
          desktop->state = STATE_PROMPT_FOCUS;
          set_status(desktop, ":f ");
        }
        if(ch == 'm') {
          desktop->state = STATE_PROMPT_MOVE;
          set_status(desktop, ":m ");
        }
        if(ch == 'r') {
          desktop->state = STATE_PROMPT_RESIZE;
          set_status(desktop, ":r ");
        }
        if(ch == 'c') {
          desktop->state = STATE_PROMPT_CLOSE;
          set_status(desktop, ":c ");
        }
        if(ch == 'o') {
          desktop->state = STATE_PROMPT_OPEN;
          set_status(desktop, ":o ");
        }
      }
    } else if(desktop->state >= STATE_PROMPT_FOCUS && desktop->state <= STATE_PROMPT_OPEN) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) { // enter
        int idx = atoi(desktop->buf);
        if(desktop->state == STATE_PROMPT_OPEN) {
          void* handle = dlopen(desktop->buf, RTLD_NOW | RTLD_LOCAL);
          if(handle) {
            if(desktop->window_count >= desktop->window_capacity) {
              desktop->window_capacity =
                desktop->window_capacity == 0 ? 4 : desktop->window_capacity * 2;
              window_t* new_windows =
                realloc(desktop->windows, sizeof(window_t) * desktop->window_capacity);
              if(!new_windows) {
                set_status(desktop, "out of memory");
                desktop->state = STATE_NORMAL;
                dlclose(handle);
                goto skip_open;
              }
              desktop->windows = new_windows;
            }
            memmove(&desktop->windows[1], &desktop->windows[0],
                    sizeof(window_t) * desktop->window_count);
            window_t* w = &desktop->windows[0];
            memset(w, 0, sizeof(window_t));
            w->handle = handle;
            dlerror();
            void (*init_fn)(desktop_t*, window_t*) = NULL;
            *(void**)(&init_fn) = dlsym(handle, "window_init");
            if(dlerror() || !init_fn) {
              memmove(&desktop->windows[0], &desktop->windows[1],
                      sizeof(window_t) * desktop->window_count);
              set_status(desktop, "failed to find window_init");
              dlclose(handle);
              desktop->state = STATE_NORMAL;
              goto skip_open;
            }
            init_fn(desktop, w);
            ++desktop->window_count;
            desktop->dispatch_window_event(desktop, w, WINDOW_EVENT_OPEN, NULL);
            set_status(desktop, "opened %s", desktop->buf);
          } else {
            set_status(desktop, "failed to load %s: %s", desktop->buf, dlerror());
          }
        skip_open:
          desktop->state = STATE_NORMAL;
        } else if(idx >= 0 && idx < desktop->window_count) {
          if(desktop->state == STATE_PROMPT_FOCUS) {
            bool ignore = desktop->dispatch_window_event(desktop, &desktop->windows[idx],
                                                         WINDOW_EVENT_FOCUS, NULL);
            if(ignore) {
              desktop->state = STATE_NORMAL;
              set_status(desktop, "window ignored focus");
            } else {
              window_t target = desktop->windows[idx];
              for(int i = idx; i > 0; --i) desktop->windows[i] = desktop->windows[i - 1];
              desktop->windows[0] = target;
              desktop->state = STATE_FOCUSED;
              desktop->target = 0;
              set_status(desktop, ":f %d (focused)", idx);
            }
          } else if(desktop->state == STATE_PROMPT_MOVE) {
            if(desktop->windows[idx].hidden) {
              desktop->state = STATE_NORMAL;
              set_status(desktop, "window is hidden");
            } else if(desktop->windows[idx].unmovable) {
              desktop->state = STATE_NORMAL;
              set_status(desktop, "window is unmovable");
            } else {
              desktop->target = idx;
              desktop->ox = desktop->windows[idx].x;
              desktop->oy = desktop->windows[idx].y;
              desktop->state = STATE_MOVING;
              set_status(desktop, ":m %d (moving)", idx);
            }
          } else if(desktop->state == STATE_PROMPT_RESIZE) {
            if(desktop->windows[idx].hidden) {
              desktop->state = STATE_NORMAL;
              set_status(desktop, "window is hidden");
            } else if(desktop->windows[idx].unresizable) {
              desktop->state = STATE_NORMAL;
              set_status(desktop, "window is unresizable");
            } else {
              desktop->target = idx;
              desktop->ow = desktop->windows[idx].w;
              desktop->oh = desktop->windows[idx].h;
              desktop->state = STATE_RESIZING;
              set_status(desktop, ":r %d (resizing)", idx);
            }
          } else if(desktop->state == STATE_PROMPT_CLOSE) {
            bool ignore = desktop->dispatch_window_event(desktop, &desktop->windows[idx],
                                                         WINDOW_EVENT_CLOSE, NULL);
            if(ignore) {
              desktop->state = STATE_NORMAL;
              set_status(desktop, "window ignored close");
            } else {
              if(desktop->windows[idx].handle) dlclose(desktop->windows[idx].handle);
              for(int i = idx; i < desktop->window_count - 1; ++i)
                desktop->windows[i] = desktop->windows[i + 1];
              --desktop->window_count;
              desktop->state = STATE_NORMAL;
              set_status(desktop, "%d window(s)", desktop->window_count);
            }
          }
        } else {
          desktop->state = STATE_NORMAL;
          set_status(desktop, "invalid index");
        }
      } else if(ch == TW_KEY_BACKSPACE || ch == 127 || ch == '\b') { // backspace
        if(desktop->buflen > 0) {
          desktop->buf[--desktop->buflen] = '\0';
          char pfx = ' ';
          if(desktop->state == STATE_PROMPT_FOCUS) pfx = 'f';
          if(desktop->state == STATE_PROMPT_MOVE) pfx = 'm';
          if(desktop->state == STATE_PROMPT_RESIZE) pfx = 'r';
          if(desktop->state == STATE_PROMPT_CLOSE) pfx = 'c';
          if(desktop->state == STATE_PROMPT_OPEN) pfx = 'o';
          set_status(desktop, ":%c %s", pfx, desktop->buf);
        }
      } else if(ch >= 32 && ch <= 126 && ch != 127 && desktop->buflen < 255) {
        desktop->buf[desktop->buflen++] = (char)ch;
        desktop->buf[desktop->buflen] = '\0';
        char pfx = ' ';
        if(desktop->state == STATE_PROMPT_FOCUS) pfx = 'f';
        if(desktop->state == STATE_PROMPT_MOVE) pfx = 'm';
        if(desktop->state == STATE_PROMPT_RESIZE) pfx = 'r';
        if(desktop->state == STATE_PROMPT_CLOSE) pfx = 'c';
        if(desktop->state == STATE_PROMPT_OPEN) pfx = 'o';
        set_status(desktop, ":%c %s", pfx, desktop->buf);
      }
    } else if(desktop->state == STATE_FOCUSED) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
        desktop->dispatch_window_event(desktop, &desktop->windows[desktop->target],
                                       WINDOW_EVENT_UNFOCUS, NULL);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else {
        desktop->dispatch_window_event(desktop, &desktop->windows[0], WINDOW_EVENT_KEY,
                                       (void*)(long)ch);
      }
    } else if(desktop->state == STATE_MOVING) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
        desktop->windows[desktop->target].x = desktop->ox;
        desktop->windows[desktop->target].y = desktop->oy;
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) {
        window_move_event_t ev = {desktop->windows[desktop->target].x - desktop->ox,
                                  desktop->windows[desktop->target].y - desktop->oy};
        desktop->dispatch_window_event(desktop, &desktop->windows[desktop->target],
                                       WINDOW_EVENT_MOVE, &ev);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if(ch == 'h' || ch == TW_KEY_LEFT) --desktop->windows[desktop->target].x;
      else if(ch == 'l' || ch == TW_KEY_RIGHT) ++desktop->windows[desktop->target].x;
      else if(ch == 'k' || ch == TW_KEY_UP) --desktop->windows[desktop->target].y;
      else if(ch == 'j' || ch == TW_KEY_DOWN) ++desktop->windows[desktop->target].y;
    } else if(desktop->state == STATE_RESIZING) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) {
        window_resize_event_t ev = {desktop->ow - desktop->windows[desktop->target].w,
                                    desktop->oh - desktop->windows[desktop->target].h};
        desktop->windows[desktop->target].w = desktop->ow;
        desktop->windows[desktop->target].h = desktop->oh;
        desktop->dispatch_window_event(desktop, &desktop->windows[desktop->target],
                                       WINDOW_EVENT_RESIZE, &ev);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if((ch == 'h' || ch == TW_KEY_LEFT) && desktop->ow > 1) --desktop->ow;
      else if(ch == 'l' || ch == TW_KEY_RIGHT) ++desktop->ow;
      else if((ch == 'k' || ch == TW_KEY_UP) && desktop->oh > 1) --desktop->oh;
      else if(ch == 'j' || ch == TW_KEY_DOWN) ++desktop->oh;
    }
  skip_key:
    call_hooks_after(&kpayload, "key");
  }
  if(desktop->window_count > 0) {
    pthread_t* threads = calloc(desktop->window_count, sizeof(pthread_t));
    window_thread_arg_t* args = calloc(desktop->window_count, sizeof(window_thread_arg_t));
    if(threads && args) {
      for(int i = 0; i < desktop->window_count; ++i) {
        args[i].window = &desktop->windows[i];
        args[i].desktop = desktop;
        if(pthread_create(&threads[i], NULL, window_update_wrapper, &args[i]) != 0) threads[i] = 0;
      }
      for(int i = 0; i < desktop->window_count; ++i)
        if(threads[i]) pthread_join(threads[i], NULL);
    }
    free(threads);
    free(args);
    for(int i = desktop->window_count - 1; i >= 0; --i) {
      if(!desktop->windows[i].close_pending) continue;
      desktop->dispatch_window_event(desktop, &desktop->windows[i], WINDOW_EVENT_CLOSE, NULL);
      if(desktop->windows[i].handle) dlclose(desktop->windows[i].handle);
      for(int j = i; j < desktop->window_count - 1; ++j)
        desktop->windows[j] = desktop->windows[j + 1];
      --desktop->window_count;
    }
  }
  call_hooks_after(&payload, "desktop_update");
  return false;
}
static void desktop_draw(desktop_t* desktop) {
  hook_payload_t payload = {.desktop = desktop, .window = NULL, .data = NULL};
  if(call_hooks_before(&payload, "desktop_draw")) return;
  tw_clear(0b01000000);
  if(desktop->window_count > 0) {
    pthread_t* threads = calloc(desktop->window_count, sizeof(pthread_t));
    window_thread_arg_t* args = calloc(desktop->window_count, sizeof(window_thread_arg_t));
    if(threads && args) {
      for(int i = desktop->window_count - 1; i >= 0; --i) {
        args[i].window = &desktop->windows[i];
        args[i].desktop = desktop;
        if(pthread_create(&threads[i], NULL, window_draw_wrapper, &args[i]) != 0) threads[i] = 0;
      }
      for(int i = desktop->window_count - 1; i >= 0; --i)
        if(threads[i]) pthread_join(threads[i], NULL);
    }
    free(threads);
    free(args);
    for(int i = desktop->window_count - 1; i >= 0; --i) {
      window_t* window = &desktop->windows[i];
      if(window->hidden) continue;
      hook_payload_t wpayload = {.desktop = desktop, .window = window, .data = (void*)(long)i};
      if(call_hooks_before(&wpayload, "desktop_window_draw")) continue;
      char title_attr =
        (desktop->state == STATE_FOCUSED && desktop->target == i) ? 0b00100000 : 0b01100000;
      int draww =
        (desktop->state == STATE_RESIZING && desktop->target == i) ? desktop->ow : window->w;
      int drawh =
        (desktop->state == STATE_RESIZING && desktop->target == i) ? desktop->oh : window->h;
      tw_fill(window->x, window->y, draww, 1, title_attr);
      char tbuf[512];
      int idx_len = snprintf(tbuf, sizeof(tbuf), "%d", i);
      if(draww >= idx_len + 2)
        snprintf(tbuf, sizeof(tbuf), "[%d] %s", i, window->title ? window->title : "");
      else if(draww >= idx_len) snprintf(tbuf, sizeof(tbuf), "%d", i);
      else if(draww > 0) {
        snprintf(tbuf, sizeof(tbuf), "%d", i);
        memmove(tbuf, tbuf + (idx_len - draww), draww + 1);
      } else tbuf[0] = '\0';
      if(draww > 0 && (int)strlen(tbuf) > draww) tbuf[draww] = '\0';
      if(tbuf[0] != '\0') tw_printf(window->x, window->y, title_attr, "%s", tbuf);
      if(!(desktop->state == STATE_RESIZING && desktop->target == i)) {
        if(window->content) {
          for(int j = 0; j < window->h; ++j) {
            for(int k = 0; k < window->w; ++k) {
              short c = window->content[j * window->w + k];
              tw_putc(c & 255, window->x + k, window->y + j + 1, c >> 8);
            }
          }
        }
      } else tw_fill(window->x, window->y + 1, draww, drawh, 0);
      call_hooks_after(&wpayload, "desktop_window_draw");
    }
  }
  if(!call_hooks_before(&payload, "status_draw")) {
    tw_wh_t size = tw_get_size();
    tw_fill(0, size.h - 1, size.w, 1, 0b01110000);
    tw_puts(desktop->statustext, 0, size.h - 1, 0b01110000);
    call_hooks_after(&payload, "status_draw");
  }
  call_hooks_after(&payload, "desktop_draw");
}
static bool dispatch_window_event(desktop_t* desktop, window_t* window, int event, void* data) {
  hook_payload_t payload = {.desktop = desktop, .window = window, .data = data};
  const char* event_names[] = {
    "window_event_none",   "window_event_key",   "window_event_move",
    "window_event_resize", "window_event_focus", "window_event_unfocus",
    "window_event_close",  "window_event_open",  "window_event_close_force",
  };
  const char* event_name = (event >= 0 && event <= 8) ? event_names[event] : "window_event_unknown";
  if(call_hooks_before(&payload, event_name)) return true;
  bool result = false;
  unsigned long long h = hash(event_name);
  pthread_mutex_lock(&hookman->lock);
  bool has_override = hm_get(hookman->hooks, h) != NULL;
  pthread_mutex_unlock(&hookman->lock);
  if(has_override) result = call_hooks(&payload, event_name);
  else result = hookman->orig_dispatch_window_event(desktop, window, event, data);
  call_hooks_after(&payload, event_name);
  return result;
}

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    if(!window->hidden) {
      window->hidden = true;
      return true;
    }
  } else if(event == WINDOW_EVENT_CLOSE_FORCE || (event == WINDOW_EVENT_CLOSE && window->hidden)) {
    if(desktop->update == desktop_update) desktop->update = hookman->orig_desktop_update;
    if(desktop->draw == desktop_draw) desktop->draw = hookman->orig_desktop_draw;
    if(desktop->dispatch_window_event == dispatch_window_event)
      desktop->dispatch_window_event = hookman->orig_dispatch_window_event;
    if(window->title) free(window->title);
    if(window->content) free(window->content);
  }
  if(event == WINDOW_EVENT_RESIZE) return true;
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)window;
  if(desktop->update != desktop_update) {
    hookman->orig_desktop_update = desktop->update;
    desktop->update = desktop_update;
  }
  if(desktop->draw != desktop_draw) {
    hookman->orig_desktop_draw = desktop->draw;
    desktop->draw = desktop_draw;
  }
  if(desktop->dispatch_window_event != dispatch_window_event) {
    hookman->orig_dispatch_window_event = desktop->dispatch_window_event;
    desktop->dispatch_window_event = dispatch_window_event;
  }
}
void draw(window_t* window, desktop_t* desktop) {
  (void)desktop;
  const char* msg = "hookman active";
  for(int i = 0; i < (int)strlen(msg) && i < window->w; ++i)
    window->content[i] = msg[i] | (0b01110000 << 8);
}
void window_init(desktop_t* desktop, window_t* win) {
  (void)desktop;
  if(hookman) {
    win->close_pending = 1;
    win->hidden = true;
    return;
  }
  win->x = 0;
  win->y = 0;
  win->w = 14;
  win->h = 1;
  win->title = strdup("hookman");
  win->content = calloc(win->w * win->h, sizeof(short));
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
  hookman_t* hm = calloc(1, sizeof(hookman_t));
  hm->magic = HOOKMAN_MAGIC;
  pthread_mutex_init(&hm->lock, NULL);
  hm->hooks = hm_create(16);
  hm->hooks_before = hm_create(16);
  hm->hooks_after = hm_create(16);
  hm->orig_desktop_update = desktop->update;
  hm->orig_desktop_draw = desktop->draw;
  hm->orig_dispatch_window_event = desktop->dispatch_window_event;
  desktop->update = desktop_update;
  desktop->draw = desktop_draw;
  desktop->dispatch_window_event = dispatch_window_event;
  hookman = hm;
  win->data = hm;
}
