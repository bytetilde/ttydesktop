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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static hookman_t* hookman = NULL;

static void hook_list_free(hook_t* list) {
  while(list) {
    hook_t* next = list->next;
    free(list);
    list = next;
  }
}
static void hm_destroy_hooks(hashmap_t* map) {
  if(!map) return;
  for(size_t i = 0; i < map->capacity; ++i)
    if(map->buckets[i].occupied) hook_list_free((hook_t*)map->buckets[i].value);
  hm_destroy(map);
}

typedef struct window_thread_arg_t {
  window_t* window;
  desktop_t* desktop;
} window_thread_arg_t;
static void set_status(desktop_t* desktop, const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  hook_payload_t payload = {.desktop = desktop, .window = NULL, .data = buf};
  if(hookman_call_hooks_before(hookman, &payload, "status_set")) return;
  if(hookman_call_hooks(hookman, &payload, "status_set"))
    ;
  else strncpy(desktop->statustext, buf, 256);
  desktop->statustimer = 3.0;
  hookman_call_hooks_after(hookman, &payload, "status_set");
}
static void desktop_open_window(desktop_t* desktop, const char* path) {
  void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  char full_path[1024];
  if(!handle && path[0] != '/' && path[0] != '.') {
    const char* home = getenv("HOME");
    char home_lib[512];
    if(home) snprintf(home_lib, sizeof(home_lib), "%s/.local/lib/ttydesktop", home);
    const char* search_paths[] = {"./bin",
                                  getenv("TTYDESKTOP_PATH"),
                                  home ? home_lib : NULL,
                                  "/usr/local/lib/ttydesktop",
                                  "/usr/lib/ttydesktop",
                                  NULL};
    for(int i = 0; search_paths[i]; ++i) {
      snprintf(full_path, sizeof(full_path), "%s/%s", search_paths[i], path);
      handle = dlopen(full_path, RTLD_NOW | RTLD_LOCAL);
      if(handle) {
        path = full_path;
        break;
      }
    }
  }
  if(handle) {
    if(desktop->window_count >= desktop->window_capacity) {
      int new_capacity = desktop->window_capacity == 0 ? 4 : desktop->window_capacity * 2;
      window_t* new_windows = realloc(desktop->windows, sizeof(window_t) * new_capacity);
      if(!new_windows) {
        set_status(desktop, "out of memory");
        dlclose(handle);
        return;
      }
      desktop->windows = new_windows;
      desktop->window_capacity = new_capacity;
    }
    memmove(&desktop->windows[1], &desktop->windows[0], sizeof(window_t) * desktop->window_count);
    window_t* w = &desktop->windows[0];
    memset(w, 0, sizeof(window_t));
    w->handle = handle;
    dlerror();
    void (*init_fn)(desktop_t*, window_t*) = NULL;
    *(void**)(&init_fn) = dlsym(handle, "window_init");
    if(dlerror() || !init_fn) {
      memmove(&desktop->windows[0], &desktop->windows[1], sizeof(window_t) * desktop->window_count);
      set_status(desktop, "failed to find window_init");
      dlclose(handle);
      return;
    }
    ++desktop->window_count;
    init_fn(desktop, w);
    desktop->dispatch_window_event(desktop, w, WINDOW_EVENT_OPEN, NULL);
    desktop->cursor_pos = 0;
    int visible = 0;
    for(int i = 0; i < desktop->window_count; ++i)
      if(!desktop->windows[i].hidden) ++visible;
    snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
  } else {
    if(access(path, F_OK) == 0) set_status(desktop, "error: %s: %s", path, dlerror());
    else set_status(desktop, "error: %s not found in lookup paths", path);
  }
}
static void* window_update_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  desktop_t* desktop = warg->desktop;
  window_t* window = warg->window;
  hook_payload_t payload = {.desktop = desktop, .window = window, .data = NULL};
  if(hookman_call_hooks_before(hookman, &payload, "window_update")) return NULL;
  if(hookman_call_hooks(hookman, &payload, "window_update"))
    ;
  else if(warg->window->update) warg->window->update(warg->window, warg->desktop);
  hookman_call_hooks_after(hookman, &payload, "window_update");
  return NULL;
}
static void* window_draw_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  desktop_t* desktop = warg->desktop;
  window_t* window = warg->window;
  if(window->hidden) return NULL;
  hook_payload_t payload = {.desktop = desktop, .window = window, .data = NULL};
  if(hookman_call_hooks_before(hookman, &payload, "window_draw")) return NULL;
  if(hookman_call_hooks(hookman, &payload, "window_draw"))
    ;
  else if(window->draw) window->draw(window, desktop);
  hookman_call_hooks_after(hookman, &payload, "window_draw");
  return NULL;
}
static bool desktop_update(desktop_t* desktop) {
  hook_payload_t payload = {.desktop = desktop, .window = NULL, .data = NULL};
  if(hookman_call_hooks_before(hookman, &payload, "desktop_update")) return false;
  if(hookman_call_hooks(hookman, &payload, "desktop_update")) return false;
  if(desktop->statustimer > 0 && desktop->state == DESKTOP_STATE_NORMAL) {
    hook_payload_t tpayload = {.desktop = desktop, .window = NULL, .data = &desktop->statustimer};
    if(hookman_call_hooks_before(hookman, &tpayload, "desktop_status_update"))
      ;
    else desktop->statustimer -= 0.033333;
    if(desktop->statustimer <= 0) {
      int visible = 0;
      for(int i = 0; i < desktop->window_count; ++i)
        if(!desktop->windows[i].hidden) ++visible;
      snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
    }
    hookman_call_hooks_after(hookman, &tpayload, "desktop_status_update");
  }
  int ch = tw_getch();
  if(ch != -1 && ch != 0) {
    hook_payload_t kpayload = {.desktop = desktop, .window = NULL, .data = (void*)(long)ch};
    if(hookman_call_hooks_before(hookman, &kpayload, "key")) goto skip_key_no_after;
    if(hookman_call_hooks(hookman, &kpayload, "key")) goto skip_key;
    if(desktop->onkey) {
      if(desktop->onkey(desktop, ch)) goto skip_key;
    } else if(desktop->state == DESKTOP_STATE_NORMAL) {
      if(ch == 'q') return true;
      else if(ch == 'f') {
        desktop->state = DESKTOP_STATE_PROMPT_FOCUS;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":f ");
        hook_payload_t spayload = {
          .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_PROMPT_FOCUS};
        hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
      } else if(ch == 'o') {
        desktop->state = DESKTOP_STATE_PROMPT_OPEN;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":o ");
        hook_payload_t spayload = {
          .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_PROMPT_OPEN};
        hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
      } else if(ch == 'm') {
        desktop->state = DESKTOP_STATE_PROMPT_MOVE;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":m ");
        hook_payload_t spayload = {
          .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_PROMPT_MOVE};
        hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
      } else if(ch == 'r') {
        desktop->state = DESKTOP_STATE_PROMPT_RESIZE;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":r ");
        hook_payload_t spayload = {
          .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_PROMPT_RESIZE};
        hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
      } else if(ch == 'c') {
        desktop->state = DESKTOP_STATE_PROMPT_CLOSE;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":c ");
        hook_payload_t spayload = {
          .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_PROMPT_CLOSE};
        hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
      }
    } else if(desktop->state >= DESKTOP_STATE_PROMPT_FOCUS &&
              desktop->state <= DESKTOP_STATE_PROMPT_OPEN) {
      if(ch == TW_KEY_ESC || ch == 27) {
        desktop->state = DESKTOP_STATE_NORMAL;
        hook_payload_t spayload = {
          .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_NORMAL};
        hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
        int visible = 0;
        for(int i = 0; i < desktop->window_count; ++i)
          if(!desktop->windows[i].hidden) ++visible;
        snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) {
        int idx = atoi(desktop->buf);
        if(desktop->state == DESKTOP_STATE_PROMPT_OPEN) {
          hook_payload_t opayload = {.desktop = desktop, .window = NULL, .data = desktop->buf};
          if(hookman_call_hooks_before(hookman, &opayload, "window_open_request")) {
            desktop->state = DESKTOP_STATE_NORMAL;
            hook_payload_t spayload = {
              .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_NORMAL};
            hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
          } else {
            desktop_open_window(desktop, desktop->buf);
            desktop->state = DESKTOP_STATE_NORMAL;
            hook_payload_t spayload = {
              .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_NORMAL};
            hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
          }
        } else if(idx >= 0 && idx < desktop->window_count) {
          if(desktop->state == DESKTOP_STATE_PROMPT_FOCUS) {
            bool ignore = desktop->dispatch_window_event(desktop, &desktop->windows[idx],
                                                         WINDOW_EVENT_FOCUS, NULL);
            if(ignore) {
              desktop->state = DESKTOP_STATE_NORMAL;
              set_status(desktop, "window ignored focus");
            } else {
              window_t target = desktop->windows[idx];
              for(int i = idx; i > 0; --i) desktop->windows[i] = desktop->windows[i - 1];
              desktop->windows[0] = target;
              desktop->state = DESKTOP_STATE_FOCUSED;
              hook_payload_t spayload = {
                .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_FOCUSED};
              hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
              desktop->target = 0;
              set_status(desktop, ":f %d (focused)", idx);
            }
          } else if(desktop->state == DESKTOP_STATE_PROMPT_MOVE) {
            if(desktop->windows[idx].hidden) {
              desktop->state = DESKTOP_STATE_NORMAL;
              set_status(desktop, "window is hidden");
            } else if(desktop->windows[idx].unmovable) {
              desktop->state = DESKTOP_STATE_NORMAL;
              set_status(desktop, "window is unmovable");
            } else {
              desktop->target = idx;
              desktop->ox = desktop->windows[idx].x;
              desktop->oy = desktop->windows[idx].y;
              desktop->state = DESKTOP_STATE_MOVING;
              hook_payload_t spayload = {
                .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_MOVING};
              hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
              set_status(desktop, ":m %d (moving)", idx);
            }
          } else if(desktop->state == DESKTOP_STATE_PROMPT_RESIZE) {
            if(desktop->windows[idx].hidden) {
              desktop->state = DESKTOP_STATE_NORMAL;
              set_status(desktop, "window is hidden");
            } else if(desktop->windows[idx].unresizable) {
              desktop->state = DESKTOP_STATE_NORMAL;
              set_status(desktop, "window is unresizable");
            } else {
              desktop->target = idx;
              desktop->ow = desktop->windows[idx].w;
              desktop->oh = desktop->windows[idx].h;
              desktop->state = DESKTOP_STATE_RESIZING;
              hook_payload_t spayload = {
                .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_RESIZING};
              hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
              set_status(desktop, ":r %d (resizing)", idx);
            }
          } else if(desktop->state == DESKTOP_STATE_PROMPT_CLOSE) {
            bool ignore = desktop->dispatch_window_event(desktop, &desktop->windows[idx],
                                                         WINDOW_EVENT_CLOSE, NULL);
            if(ignore) {
              desktop->state = DESKTOP_STATE_NORMAL;
              set_status(desktop, "window ignored close");
            } else {
              if(desktop->windows[idx].handle && desktop->windows[idx].data != hookman)
                dlclose(desktop->windows[idx].handle);
              for(int i = idx; i < desktop->window_count - 1; ++i)
                desktop->windows[i] = desktop->windows[i + 1];
              --desktop->window_count;
              desktop->state = DESKTOP_STATE_NORMAL;
              int visible = 0;
              for(int i = 0; i < desktop->window_count; ++i)
                if(!desktop->windows[i].hidden) ++visible;
              snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count,
                       visible);
            }
          }
          hook_payload_t spayload = {
            .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_NORMAL};
          hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
        } else {
          desktop->state = DESKTOP_STATE_NORMAL;
          set_status(desktop, "invalid index");
        }
      } else if(ch == TW_KEY_LEFT) {
        if(desktop->cursor_pos > 0) --desktop->cursor_pos;
      } else if(ch == TW_KEY_RIGHT) {
        if(desktop->cursor_pos < desktop->buflen) ++desktop->cursor_pos;
      } else if(ch == TW_KEY_BACKSPACE || ch == 127 || ch == 8) {
        if(desktop->cursor_pos > 0) {
          memmove(&desktop->buf[desktop->cursor_pos - 1], &desktop->buf[desktop->cursor_pos],
                  desktop->buflen - desktop->cursor_pos + 1);
          --desktop->buflen;
          --desktop->cursor_pos;
        }
      } else if(ch >= 32 && ch <= 126 && desktop->buflen < 254) {
        memmove(&desktop->buf[desktop->cursor_pos + 1], &desktop->buf[desktop->cursor_pos],
                desktop->buflen - desktop->cursor_pos + 1);
        desktop->buf[desktop->cursor_pos++] = (char)ch;
        ++desktop->buflen;
      }
      if(desktop->state >= DESKTOP_STATE_PROMPT_FOCUS &&
         desktop->state <= DESKTOP_STATE_PROMPT_OPEN) {
        char pfx = ' ';
        switch(desktop->state) {
          case DESKTOP_STATE_PROMPT_FOCUS: pfx = 'f'; break;
          case DESKTOP_STATE_PROMPT_OPEN: pfx = 'o'; break;
          case DESKTOP_STATE_PROMPT_MOVE: pfx = 'm'; break;
          case DESKTOP_STATE_PROMPT_RESIZE: pfx = 'r'; break;
          case DESKTOP_STATE_PROMPT_CLOSE: pfx = 'c'; break;
          default: pfx = '?'; break;
        }
        set_status(desktop, ":%c %s", pfx, desktop->buf);
      }
    } else if(desktop->state == DESKTOP_STATE_FOCUSED) {
      if(ch == TW_KEY_ESC || ch == 27) {
        bool ignore = desktop->dispatch_window_event(desktop, &desktop->windows[desktop->target],
                                                     WINDOW_EVENT_UNFOCUS, NULL);
        if(!ignore) {
          desktop->state = DESKTOP_STATE_NORMAL;
          int visible = 0;
          for(int i = 0; i < desktop->window_count; ++i)
            if(!desktop->windows[i].hidden) ++visible;
          snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
        }
      } else {
        desktop->dispatch_window_event(desktop, &desktop->windows[0], WINDOW_EVENT_KEY,
                                       (void*)(long)ch);
      }
    } else if(desktop->state == DESKTOP_STATE_MOVING) {
      if(ch == TW_KEY_ESC || ch == 27) {
        desktop->windows[desktop->target].x = desktop->ox;
        desktop->windows[desktop->target].y = desktop->oy;
        desktop->state = DESKTOP_STATE_NORMAL;
        int visible = 0;
        for(int i = 0; i < desktop->window_count; ++i)
          if(!desktop->windows[i].hidden) ++visible;
        snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) {
        window_move_event_t ev = {desktop->windows[desktop->target].x - desktop->ox,
                                  desktop->windows[desktop->target].y - desktop->oy};
        desktop->dispatch_window_event(desktop, &desktop->windows[desktop->target],
                                       WINDOW_EVENT_MOVE, &ev);
        desktop->state = DESKTOP_STATE_NORMAL;
        int visible = 0;
        for(int i = 0; i < desktop->window_count; ++i)
          if(!desktop->windows[i].hidden) ++visible;
        snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
      } else if(ch == 'h' || ch == TW_KEY_LEFT) --desktop->windows[desktop->target].x;
      else if(ch == 'l' || ch == TW_KEY_RIGHT) ++desktop->windows[desktop->target].x;
      else if(ch == 'k' || ch == TW_KEY_UP) --desktop->windows[desktop->target].y;
      else if(ch == 'j' || ch == TW_KEY_DOWN) ++desktop->windows[desktop->target].y;
      hook_payload_t prpayload = {
        .desktop = desktop, .window = &desktop->windows[desktop->target], .data = (void*)(long)ch};
      hookman_call_hooks_after(hookman, &prpayload, "window_move_preview");
    } else if(desktop->state == DESKTOP_STATE_RESIZING) {
      if(ch == TW_KEY_ESC || ch == 27) {
        desktop->state = DESKTOP_STATE_NORMAL;
        int visible = 0;
        for(int i = 0; i < desktop->window_count; ++i)
          if(!desktop->windows[i].hidden) ++visible;
        snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) {
        window_resize_event_t ev = {desktop->ow - desktop->windows[desktop->target].w,
                                    desktop->oh - desktop->windows[desktop->target].h};
        desktop->windows[desktop->target].w = desktop->ow;
        desktop->windows[desktop->target].h = desktop->oh;
        desktop->dispatch_window_event(desktop, &desktop->windows[desktop->target],
                                       WINDOW_EVENT_RESIZE, &ev);
        desktop->state = DESKTOP_STATE_NORMAL;
        int visible = 0;
        for(int i = 0; i < desktop->window_count; ++i)
          if(!desktop->windows[i].hidden) ++visible;
        snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
      } else if((ch == 'h' || ch == TW_KEY_LEFT) && desktop->ow > 1) --desktop->ow;
      else if(ch == 'l' || ch == TW_KEY_RIGHT) ++desktop->ow;
      else if((ch == 'k' || ch == TW_KEY_UP) && desktop->oh > 1) --desktop->oh;
      else if(ch == 'j' || ch == TW_KEY_DOWN) ++desktop->oh;
      hook_payload_t prpayload = {
        .desktop = desktop, .window = &desktop->windows[desktop->target], .data = (void*)(long)ch};
      hookman_call_hooks_after(hookman, &prpayload, "window_resize_preview");
    }
  skip_key:
    hookman_call_hooks_after(hookman, &kpayload, "key");
  skip_key_no_after:;
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
      bool ignore =
        desktop->dispatch_window_event(desktop, &desktop->windows[i], WINDOW_EVENT_CLOSE, NULL);
      if(ignore) {
        desktop->windows[i].close_pending = false;
        continue;
      }
      if(desktop->state != DESKTOP_STATE_NORMAL) {
        if(desktop->target == i) {
          desktop->state = DESKTOP_STATE_NORMAL;
          hook_payload_t spayload = {
            .desktop = desktop, .window = NULL, .data = (void*)DESKTOP_STATE_NORMAL};
          hookman_call_hooks_after(hookman, &spayload, "desktop_state_change");
          int visible = 0;
          for(int i = 0; i < desktop->window_count; ++i)
            if(!desktop->windows[i].hidden) ++visible;
          snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
        } else if(desktop->target > i) --desktop->target;
      }
      if(desktop->windows[i].handle && desktop->windows[i].data != hookman)
        dlclose(desktop->windows[i].handle);
      for(int j = i; j < desktop->window_count - 1; ++j)
        desktop->windows[j] = desktop->windows[j + 1];
      --desktop->window_count;
    }
  }
  hookman_call_hooks_after(hookman, &payload, "desktop_update");
  return false;
}
static void desktop_draw(desktop_t* desktop) {
  hook_payload_t payload = {.desktop = desktop, .window = NULL, .data = NULL};
  if(hookman_call_hooks_before(hookman, &payload, "desktop_draw")) return;
  if(hookman_call_hooks_before(hookman, &payload, "desktop_wall")) return;
  if(hookman_call_hooks(hookman, &payload, "desktop_wall"))
    ;
  else tw_clear(0b01000000);
  hookman_call_hooks_after(hookman, &payload, "desktop_wall");
  if(hookman_call_hooks_before(hookman, &payload, "desktop_draw_windows")) return;
  if(hookman_call_hooks(hookman, &payload, "desktop_draw_windows"))
    ;
  else if(desktop->window_count > 0) {
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
      if(hookman_call_hooks_before(hookman, &wpayload, "desktop_window_draw")) continue;
      if(hookman_call_hooks(hookman, &wpayload, "desktop_window_draw"))
        ;
      else {
        char title_attr = (desktop->state == DESKTOP_STATE_FOCUSED && desktop->target == i)
                            ? 0b00100000
                            : 0b01100000;
        int draww = (desktop->state == DESKTOP_STATE_RESIZING && desktop->target == i) ? desktop->ow
                                                                                       : window->w;
        int drawh = (desktop->state == DESKTOP_STATE_RESIZING && desktop->target == i) ? desktop->oh
                                                                                       : window->h;
        hook_payload_t tpayload = {.desktop = desktop, .window = window, .data = (void*)(long)i};
        if(hookman_call_hooks_before(hookman, &tpayload, "desktop_window_title")) goto skip_title;
        else if(hookman_call_hooks(hookman, &tpayload, "desktop_window_title"))
          ;
        else {
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
          if(draww > 0 && draww < (int)sizeof(tbuf)) tbuf[draww] = '\0';
          if(tbuf[0] != '\0') tw_printf(window->x, window->y, title_attr, "%s", tbuf);
        }
        hookman_call_hooks_after(hookman, &tpayload, "desktop_window_title");
      skip_title:;
        if(!(desktop->state == DESKTOP_STATE_RESIZING && desktop->target == i)) {
          if(window->content) {
            for(int j = 0; j < window->h; ++j) {
              for(int k = 0; k < window->w; ++k) {
                short c = window->content[j * window->w + k];
                tw_putc(c & 255, window->x + k, window->y + j + 1, c >> 8);
              }
            }
          }
        } else tw_fill(window->x, window->y + 1, draww, drawh, 0);
      }
      hookman_call_hooks_after(hookman, &wpayload, "desktop_window_draw");
    }
  }
  if(hookman_call_hooks_before(hookman, &payload, "desktop_status_draw")) return;
  if(hookman_call_hooks(hookman, &payload, "desktop_status_draw"))
    ;
  else {
    tw_wh_t size = tw_get_size();
    tw_fill(0, size.h - 1, size.w, 1, 0b01110000);
    tw_puts(desktop->statustext, 0, size.h - 1, 0b01110000);
    if(desktop->state >= DESKTOP_STATE_PROMPT_FOCUS && desktop->state <= DESKTOP_STATE_PROMPT_OPEN)
      tw_putc(' ', 3 + desktop->cursor_pos, size.h - 1, 0b00000111);
  }
  hookman_call_hooks_after(hookman, &payload, "desktop_status_draw");
  hookman_call_hooks_after(hookman, &payload, "desktop_draw");
}
static bool desktop_flush(desktop_t* desktop) {
  hook_payload_t payload = {.desktop = desktop, .window = NULL, .data = NULL};
  if(hookman_call_hooks_before(hookman, &payload, "desktop_flush")) return true;
  if(hookman_call_hooks(hookman, &payload, "desktop_flush"))
    ;
  else {
    tw_flush();
    usleep(33333);
  }
  hookman_call_hooks_after(hookman, &payload, "desktop_flush");
  return false;
}
static bool dispatch_window_event(desktop_t* desktop, window_t* window, int event, void* data) {
  hook_payload_t payload = {.desktop = desktop, .window = window, .data = data};
  const char* event_names[] = {
    "window_event_none",   "window_event_key",   "window_event_move",
    "window_event_resize", "window_event_focus", "window_event_unfocus",
    "window_event_close",  "window_event_open",  "window_event_close_force",
  };
  const char* event_name = (event >= 0 && event <= 8) ? event_names[event] : "window_event_unknown";
  if(!hookman) return window->onevent ? window->onevent(window, desktop, event, data) : false;
  if(hookman_call_hooks_before(hookman, &payload, event_name)) return true;
  bool result = false;
  // unsigned long long h = hash(event_name);
  // pthread_mutex_lock(&hookman->lock);
  // bool has_override = hm_get(hookman->hooks, h) != NULL;
  // pthread_mutex_unlock(&hookman->lock);
  // if(has_override) result = hookman_call_hooks(hookman, &payload, event_name);
  if(hookman_hooks_exist(hookman, event_name))
    result = hookman_call_hooks(hookman, &payload, event_name);
  else if(hookman->orig_dispatch_window_event)
    result = hookman->orig_dispatch_window_event(desktop, window, event, data);
  else result = window->onevent ? window->onevent(window, desktop, event, data) : false;
  hookman_call_hooks_after(hookman, &payload, event_name);
  return result;
}

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  (void)data;
  if(event == WINDOW_EVENT_CLOSE) {
    if(desktop->update == desktop_update) desktop->update = hookman->orig_desktop_update;
    if(desktop->draw == desktop_draw) desktop->draw = hookman->orig_desktop_draw;
    if(desktop->dispatch_window_event == dispatch_window_event)
      desktop->dispatch_window_event = hookman->orig_dispatch_window_event;
    if(window->title) {
      free(window->title);
      window->title = NULL;
    }
    if(window->content) {
      free(window->content);
      window->content = NULL;
    }
    if(window->data) {
      hookman_t* hm = (hookman_t*)window->data;
      hm_destroy_hooks(hm->hooks);
      hm_destroy_hooks(hm->hooks_before);
      hm_destroy_hooks(hm->hooks_after);
      hm_destroy(hm->exports);
      free(hm);
      window->data = NULL;
      hookman = NULL;
    }
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
  if(desktop->flush != desktop_flush) {
    hookman->orig_desktop_flush = desktop->flush;
    desktop->flush = desktop_flush;
  }
  if(desktop->dispatch_window_event != dispatch_window_event) {
    hookman->orig_dispatch_window_event = desktop->dispatch_window_event;
    desktop->dispatch_window_event = dispatch_window_event;
  }
}
void draw(window_t* window, desktop_t* desktop) {
  (void)window;
  (void)desktop;
}
void window_init(desktop_t* desktop, window_t* win) {
  (void)desktop;
  win->hidden = true;
  if(hookman) {
    win->close_pending = 1;
    return;
  }
  win->title = strdup("hookman");
  win->update = update;
  win->draw = draw;
  win->onevent = onevent;
  hookman_t* hm = calloc(1, sizeof(hookman_t));
  hm->magic = HOOKMAN_MAGIC;
  pthread_mutexattr_t attr;
  pthread_mutexattr_init(&attr);
  pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&hm->lock, &attr);
  pthread_mutexattr_destroy(&attr);
  hm->hooks = hm_create(16);
  hm->hooks_before = hm_create(16);
  hm->hooks_after = hm_create(16);
  hm->exports = hm_create(16);
  hm->orig_desktop_update = desktop->update;
  hm->orig_desktop_draw = desktop->draw;
  hm->orig_desktop_flush = desktop->flush;
  hm->orig_dispatch_window_event = desktop->dispatch_window_event;
  desktop->update = desktop_update;
  desktop->draw = desktop_draw;
  desktop->flush = desktop_flush;
  desktop->dispatch_window_event = dispatch_window_event;
  hookman = hm;
  win->data = hm;
}
