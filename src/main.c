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
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct window_thread_arg_t {
  window_t* window;
  desktop_t* desktop;
} window_thread_arg_t;
void* window_update_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  if(warg->window->update) warg->window->update(warg->window, warg->desktop);
  return NULL;
}
void* window_draw_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  desktop_t* desktop = warg->desktop;
  window_t* window = warg->window;
  if(window->hidden) return NULL;
  if(window->draw) window->draw(window, desktop);
  return NULL;
}
void set_status(desktop_t* desktop, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(desktop->statustext, 256, fmt, args);
  va_end(args);
  desktop->statustimer = 3.0;
}
bool dispatch_window_event(desktop_t* desktop, window_t* window, int event, void* data) {
  bool ignore = false;
  if(window->onevent) ignore = window->onevent(window, desktop, event, data);
  return ignore;
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
      desktop->window_capacity = desktop->window_capacity == 0 ? 4 : desktop->window_capacity * 2;
      window_t* new_windows =
        realloc(desktop->windows, sizeof(window_t) * desktop->window_capacity);
      if(!new_windows) {
        set_status(desktop, "error: out of memory");
        dlclose(handle);
        return;
      }
      desktop->windows = new_windows;
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
      set_status(desktop, "error: %s: window_init not found", path);
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
    // Check if file exists but failed to load, or just missing
    if(access(path, F_OK) == 0) set_status(desktop, "error: %s: %s", path, dlerror());
    else set_status(desktop, "error: %s not found in lookup paths", path);
  }
}
static void desktop_load_autostart_config(desktop_t* desktop) {
  const char* home = getenv("HOME");
  char home_config[512] = "";
  if(home) snprintf(home_config, sizeof(home_config), "%s/.config/ttydesktop/autostart.conf", home);
  const char* config_files[] = {home ? home_config : NULL, "/etc/ttydesktop/autostart.conf",
                                "autostart.conf", NULL};
  FILE* f = NULL;
  for(int i = 0; config_files[i]; ++i) {
    if(config_files[i][0] == '\0') continue;
    f = fopen(config_files[i], "r");
    if(f) break;
  }
  if(!f) return;
  char line[256];
  while(fgets(line, sizeof(line), f)) {
    char* p = line;
    while(*p && (*p == ' ' || *p == '\t')) ++p;
    if(*p == '#' || *p == '\0' || *p == '\n') continue;
    char* end = p + strlen(p) - 1;
    while(end > p && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) *end-- = '\0';
    if(*p) desktop_open_window(desktop, p);
  }
  fclose(f);
}
static void desktop_close_window(desktop_t* desktop, int index) {
  if(index < 0 || index >= desktop->window_count) return;
  desktop->windows[index].close_pending = true;
}
bool desktop_update(desktop_t* desktop) {
  if(desktop->statustimer > 0 && desktop->state != DESKTOP_STATE_MOVING &&
     desktop->state != DESKTOP_STATE_RESIZING && desktop->state != DESKTOP_STATE_FOCUSED) {
    desktop->statustimer -= 0.033333;
    if(desktop->statustimer <= 0) {
      int visible = 0;
      for(int i = 0; i < desktop->window_count; ++i)
        if(!desktop->windows[i].hidden) ++visible;
      snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
    }
  }
  int ch = tw_getch();
  if(ch != -1 && ch != 0) {
    if(desktop->onkey) {
      if(desktop->onkey(desktop, ch)) goto skip_key;
    } else if(desktop->state == DESKTOP_STATE_NORMAL) {
      if(ch == 'q') return true;
      else if(ch == 'f') { // focus
        desktop->state = DESKTOP_STATE_PROMPT_FOCUS;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":f ");
      } else if(ch == 'o') { // open
        desktop->state = DESKTOP_STATE_PROMPT_OPEN;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":o ");
      } else if(ch == 'm') { // move
        desktop->state = DESKTOP_STATE_PROMPT_MOVE;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":m ");
      } else if(ch == 'r') { // resize
        desktop->state = DESKTOP_STATE_PROMPT_RESIZE;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":r ");
      } else if(ch == 'c') { // close
        desktop->state = DESKTOP_STATE_PROMPT_CLOSE;
        desktop->buflen = 0;
        desktop->cursor_pos = 0;
        desktop->buf[0] = '\0';
        set_status(desktop, ":c ");
      }
    } else if(desktop->state >= DESKTOP_STATE_PROMPT_FOCUS &&
              desktop->state <= DESKTOP_STATE_PROMPT_OPEN) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
        desktop->state = DESKTOP_STATE_NORMAL;
        int visible = 0;
        for(int i = 0; i < desktop->window_count; ++i)
          if(!desktop->windows[i].hidden) ++visible;
        snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) { // enter
        int idx = atoi(desktop->buf);
        if(desktop->state == DESKTOP_STATE_PROMPT_OPEN) {
          desktop_open_window(desktop, desktop->buf);
          desktop->state = DESKTOP_STATE_NORMAL;
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
              set_status(desktop, ":r %d (resizing)", idx);
            }
          } else if(desktop->state == DESKTOP_STATE_PROMPT_CLOSE) {
            bool ignore = desktop->dispatch_window_event(desktop, &desktop->windows[idx],
                                                         WINDOW_EVENT_CLOSE, NULL);
            if(ignore) {
              desktop->state = DESKTOP_STATE_NORMAL;
              set_status(desktop, "window ignored close");
            } else {
              if(desktop->windows[idx].handle) dlclose(desktop->windows[idx].handle);
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
      } else if(ch >= 32 && ch <= 126 && desktop->buflen < 255) {
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
      if(ch == TW_KEY_ESC || ch == 27) { // esc
        desktop->dispatch_window_event(desktop, &desktop->windows[desktop->target],
                                       WINDOW_EVENT_UNFOCUS, NULL);
        desktop->state = DESKTOP_STATE_NORMAL;
        int visible = 0;
        for(int i = 0; i < desktop->window_count; ++i)
          if(!desktop->windows[i].hidden) ++visible;
        snprintf(desktop->statustext, 256, "%d apps, %d visible", desktop->window_count, visible);
      } else {
        desktop->dispatch_window_event(desktop, &desktop->windows[0], WINDOW_EVENT_KEY,
                                       (void*)(long)ch);
      }
    } else if(desktop->state == DESKTOP_STATE_MOVING) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
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
    } else if(desktop->state == DESKTOP_STATE_RESIZING) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
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
    }
  skip_key:;
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
      if(desktop->windows[i].handle) dlclose(desktop->windows[i].handle);
      for(int j = i; j < desktop->window_count - 1; ++j)
        desktop->windows[j] = desktop->windows[j + 1];
      --desktop->window_count;
    }
  }
  return false;
}
void desktop_draw(desktop_t* desktop) {
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
      char title_attr =
        (desktop->state == DESKTOP_STATE_FOCUSED && desktop->target == i) ? 0b00100000 : 0b01100000;
      int draww = (desktop->state == DESKTOP_STATE_RESIZING && desktop->target == i) ? desktop->ow
                                                                                     : window->w;
      int drawh = (desktop->state == DESKTOP_STATE_RESIZING && desktop->target == i) ? desktop->oh
                                                                                     : window->h;
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
  }
  tw_wh_t size = tw_get_size();
  tw_fill(0, size.h - 1, size.w, 1, 0b01110000);
  tw_puts(desktop->statustext, 0, size.h - 1, 0b01110000);
  if(desktop->state >= DESKTOP_STATE_PROMPT_FOCUS && desktop->state <= DESKTOP_STATE_PROMPT_OPEN)
    tw_putc(' ', 3 + desktop->cursor_pos, size.h - 1, 0b00000111);
}

int main(int argc, char** argv) {
  printf("ttydesktop  Copyright (C) 2026  bytetilde\n");
  printf("This program comes with ABSOLUTELY NO WARRANTY; for details see the GNU General Public "
         "License (version 3).\n");
  printf("This is free software, and you are welcome to redistribute it\n");
  printf("under the terms of the GNU General Public License (version 3).\n");
  desktop_t desktop = {
    .windows = NULL,
    .window_count = 0,
    .window_capacity = 0,
    .state = DESKTOP_STATE_NORMAL,
    .buflen = 0,
    .target = 0,
    .ox = 0,
    .oy = 0,
    .ow = 0,
    .oh = 0,
    .update = desktop_update,
    .draw = desktop_draw,
    .flush = NULL,
    .onkey = NULL,
    .close_window = desktop_close_window,
    .dispatch_window_event = dispatch_window_event,
  };
  desktop.statustext = calloc(256, sizeof(char));
  if(!desktop.statustext) {
    fprintf(stderr, "failed to allocate statustext\n");
    return 1;
  }
  tw_init();
  desktop_load_autostart_config(&desktop);
  for(int i = 1; i < argc; ++i) desktop_open_window(&desktop, argv[i]);
  snprintf(desktop.statustext, 256, "hello tty desktop");
  while(1) {
    if(desktop.update(&desktop)) break;
    desktop.draw(&desktop);
    if(desktop.flush) {
      if(desktop.flush(&desktop)) break;
    } else {
      tw_flush();
      usleep(33333);
    }
  }
  for(int i = desktop.window_count - 1; i >= 0; --i)
    if(desktop.dispatch_window_event(&desktop, &desktop.windows[i], WINDOW_EVENT_CLOSE, NULL))
      desktop.dispatch_window_event(&desktop, &desktop.windows[i], WINDOW_EVENT_CLOSE_FORCE, NULL);
  for(int i = desktop.window_count - 1; i >= 0; --i)
    if(desktop.windows[i].handle) dlclose(desktop.windows[i].handle);
  if(desktop.windows) free(desktop.windows);
  if(desktop.statustext) free(desktop.statustext);
  tw_deinit();
  return 0;
}
