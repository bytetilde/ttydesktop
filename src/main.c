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
  int index;
} window_thread_arg_t;
void* window_update_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  if(warg->desktop->before_window_update)
    warg->desktop->before_window_update(warg->desktop, warg->window, warg->index);
  if(warg->window->update) warg->window->update(warg->window, warg->desktop);
  if(warg->desktop->after_window_update)
    warg->desktop->after_window_update(warg->desktop, warg->window, warg->index);
  return NULL;
}
void* window_draw_wrapper(void* arg) {
  window_thread_arg_t* warg = arg;
  desktop_t* desktop = warg->desktop;
  window_t* window = warg->window;
  int i = warg->index;
  if(window->hidden) return NULL;
  if(desktop->before_window_draw) desktop->before_window_draw(desktop, window, i);
  if(window->draw) window->draw(window, desktop);
  return NULL;
}
void set_status(desktop_t* desktop, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(desktop->statustext, 256, fmt, args);
  va_end(args);
  if(desktop->on_status_change) desktop->on_status_change(desktop, desktop->statustext);
}
bool dispatch_window_event(desktop_t* desktop, window_t* window, int index, int event, void* data) {
  if(desktop->before_window_event &&
     desktop->before_window_event(desktop, window, index, event, data))
    return true;
  bool ignore = false;
  if(window->onevent) ignore = window->onevent(window, desktop, event, data);
  if(desktop->after_window_event) desktop->after_window_event(desktop, window, index, event, data);
  return ignore;
}
static void desktop_close_window(desktop_t* desktop, int index) {
  if(index < 0 || index >= desktop->window_count) return;
  desktop->windows[index].close_pending = true;
}
bool desktop_update(desktop_t* desktop) {
  if(desktop->before_update && desktop->before_update(desktop)) return true;
  int ch = tw_getch();
  if(ch != -1 && ch != 0) {
    if(desktop->before_key && desktop->before_key(desktop, ch)) goto skip_key;
    if(desktop->on_key) {
      if(desktop->on_key(desktop, ch)) goto skip_key;
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
            void (*init_fn)(window_t*) = NULL;
            *(void**)(&init_fn) = dlsym(handle, "window_init");
            if(dlerror() || !init_fn) {
              memmove(&desktop->windows[0], &desktop->windows[1],
                      sizeof(window_t) * desktop->window_count);
              set_status(desktop, "failed to find window_init");
              dlclose(handle);
              desktop->state = STATE_NORMAL;
              goto skip_open;
            }
            init_fn(w);
            ++desktop->window_count;
            dispatch_window_event(desktop, w, 0, WINDOW_EVENT_OPEN, NULL);
            set_status(desktop, "opened %s", desktop->buf);
          } else {
            set_status(desktop, "failed to load %s: %s", desktop->buf, dlerror());
          }
        skip_open:
          desktop->state = STATE_NORMAL;
        } else if(idx >= 0 && idx < desktop->window_count) {
          if(desktop->state == STATE_PROMPT_FOCUS) {
            bool ignore =
              dispatch_window_event(desktop, &desktop->windows[idx], idx, WINDOW_EVENT_FOCUS, NULL);
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
            bool ignore =
              dispatch_window_event(desktop, &desktop->windows[idx], idx, WINDOW_EVENT_CLOSE, NULL);
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
        dispatch_window_event(desktop, &desktop->windows[desktop->target], desktop->target,
                              WINDOW_EVENT_UNFOCUS, NULL);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else {
        dispatch_window_event(desktop, &desktop->windows[0], 0, WINDOW_EVENT_KEY, (void*)(long)ch);
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
        dispatch_window_event(desktop, &desktop->windows[desktop->target], desktop->target,
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
        dispatch_window_event(desktop, &desktop->windows[desktop->target], desktop->target,
                              WINDOW_EVENT_RESIZE, &ev);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if((ch == 'h' || ch == TW_KEY_LEFT) && desktop->ow > 1) --desktop->ow;
      else if(ch == 'l' || ch == TW_KEY_RIGHT) ++desktop->ow;
      else if((ch == 'k' || ch == TW_KEY_UP) && desktop->oh > 1) --desktop->oh;
      else if(ch == 'j' || ch == TW_KEY_DOWN) ++desktop->oh;
    }
    if(desktop->after_key) desktop->after_key(desktop, ch);
  skip_key:;
  }
  if(desktop->window_count > 0) {
    pthread_t* threads = calloc(desktop->window_count, sizeof(pthread_t));
    window_thread_arg_t* args = calloc(desktop->window_count, sizeof(window_thread_arg_t));
    if(threads && args) {
      for(int i = 0; i < desktop->window_count; ++i) {
        args[i].window = &desktop->windows[i];
        args[i].desktop = desktop;
        args[i].index = i;
        if(pthread_create(&threads[i], NULL, window_update_wrapper, &args[i]) != 0) threads[i] = 0;
      }
      for(int i = 0; i < desktop->window_count; ++i)
        if(threads[i]) pthread_join(threads[i], NULL);
    }
    free(threads);
    free(args);
    for(int i = desktop->window_count - 1; i >= 0; --i) {
      if(!desktop->windows[i].close_pending) continue;
      dispatch_window_event(desktop, &desktop->windows[i], i, WINDOW_EVENT_CLOSE, NULL);
      if(desktop->windows[i].handle) dlclose(desktop->windows[i].handle);
      for(int j = i; j < desktop->window_count - 1; ++j) desktop->windows[j] = desktop->windows[j + 1];
      --desktop->window_count;
    }
  }
  if(desktop->after_update && desktop->after_update(desktop)) return true;
  return false;
}
void desktop_draw(desktop_t* desktop) {
  if(desktop->before_draw) desktop->before_draw(desktop);
  tw_clear(0b01000000);
  if(desktop->window_count > 0) {
    pthread_t* threads = calloc(desktop->window_count, sizeof(pthread_t));
    window_thread_arg_t* args = calloc(desktop->window_count, sizeof(window_thread_arg_t));
    if(threads && args) {
      for(int i = desktop->window_count - 1; i >= 0; --i) {
        args[i].window = &desktop->windows[i];
        args[i].desktop = desktop;
        args[i].index = i;
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
      if(desktop->after_window_draw) desktop->after_window_draw(desktop, window, i);
    }
  }
  if(desktop->before_draw_status_bar) desktop->before_draw_status_bar(desktop);
  if(desktop->draw_status_bar) desktop->draw_status_bar(desktop);
  else {
    tw_wh_t size = tw_get_size();
    tw_fill(0, size.h - 1, size.w, 1, 0b01110000);
    tw_puts(desktop->statustext, 0, size.h - 1, 0b01110000);
  }
  // if(desktop->after_draw_status_bar) desktop->after_draw_status_bar(desktop);
  if(desktop->after_draw) desktop->after_draw(desktop);
}

int main() {
  tw_init();
  desktop_t desktop = {
    .windows = NULL,
    .window_count = 0,
    .window_capacity = 0,
    .close_window = desktop_close_window,
    .before_update = NULL,
    .update = desktop_update,
    .after_update = NULL,
    .before_draw = NULL,
    .draw = desktop_draw,
    .after_draw = NULL,
    .state = STATE_NORMAL,
  };
  desktop.statustext = calloc(256, sizeof(char));
  snprintf(desktop.statustext, 256, "hello tty desktop");
  while(1) {
    if(desktop.update(&desktop)) break;
    desktop.draw(&desktop);
    tw_flush();
    usleep(33333);
  }
  for(int i = desktop.window_count - 1; i >= 0; --i) {
    if(dispatch_window_event(&desktop, &desktop.windows[i], i, WINDOW_EVENT_CLOSE, NULL))
      dispatch_window_event(&desktop, &desktop.windows[i], i, WINDOW_EVENT_CLOSE_FORCE, NULL);
    if(desktop.windows[i].handle) dlclose(desktop.windows[i].handle);
  }
  if(desktop.windows) free(desktop.windows);
  if(desktop.statustext) free(desktop.statustext);
  tw_deinit();
  return 0;
}
