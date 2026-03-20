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
  if(warg->window->update) warg->window->update(warg->window, warg->desktop);
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
  char title_attr =
    (desktop->state == STATE_FOCUSED && desktop->target == i) ? 0b00100000 : 0b00110000;
  int draww = (desktop->state == STATE_RESIZING && desktop->target == i) ? desktop->ow : window->w;
  int drawh = (desktop->state == STATE_RESIZING && desktop->target == i) ? desktop->oh : window->h;
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
  return NULL;
}
void set_status(desktop_t* desktop, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(desktop->statustext, 256, fmt, args);
  va_end(args);
}
bool desktop_update(desktop_t* desktop) {
  if(desktop->before_update && desktop->before_update(desktop)) return true;
  int ch = tw_getch();
  if(ch != -1 && ch != 0) {
    if(desktop->state == STATE_NORMAL) {
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
            void (*init_fn)(window_t*) = dlsym(handle, "window_init");
            if(init_fn) {
              desktop->windows =
                realloc(desktop->windows, sizeof(window_t) * (desktop->window_count + 1));
              window_t* w = &desktop->windows[desktop->window_count];
              memset(w, 0, sizeof(window_t));
              init_fn(w);
              ++desktop->window_count;
              if(w->onevent) w->onevent(w, desktop, WINDOW_EVENT_OPEN, NULL);
              set_status(desktop, "opened %s", desktop->buf);
            } else {
              set_status(desktop, "failed to find window_init");
              dlclose(handle);
            }
          } else {
            set_status(desktop, "failed to load %s", desktop->buf);
          }
          desktop->state = STATE_NORMAL;
        } else if(idx >= 0 && idx < desktop->window_count) {
          if(desktop->state == STATE_PROMPT_FOCUS) {
            bool ignore = false;
            if(desktop->windows[idx].onevent) {
              ignore = desktop->windows[idx].onevent(&desktop->windows[idx], desktop,
                                                     WINDOW_EVENT_FOCUS, NULL);
            }
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
            bool ignore = false;
            if(desktop->windows[idx].onevent)
              ignore = desktop->windows[idx].onevent(&desktop->windows[idx], desktop,
                                                     WINDOW_EVENT_CLOSE, NULL);
            if(ignore) {
              desktop->state = STATE_NORMAL;
              set_status(desktop, "window ignored close");
            } else {
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
      } else if(desktop->buflen < 255) {
        desktop->buf[desktop->buflen++] = ch;
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
        if(desktop->windows[desktop->target].onevent)
          desktop->windows[desktop->target].onevent(&desktop->windows[desktop->target], desktop,
                                                    WINDOW_EVENT_UNFOCUS, NULL);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if(desktop->windows[0].onevent) {
        desktop->windows[0].onevent(&desktop->windows[0], desktop, WINDOW_EVENT_KEY,
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
        if(desktop->windows[desktop->target].onevent)
          desktop->windows[desktop->target].onevent(&desktop->windows[desktop->target], desktop,
                                                    WINDOW_EVENT_MOVE, &ev);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if(ch == 'h' || ch == TW_KEY_LEFT) {
        --desktop->windows[desktop->target].x;
      } else if(ch == 'l' || ch == TW_KEY_RIGHT) {
        ++desktop->windows[desktop->target].x;
      } else if(ch == 'k' || ch == TW_KEY_UP) {
        --desktop->windows[desktop->target].y;
      } else if(ch == 'j' || ch == TW_KEY_DOWN) {
        ++desktop->windows[desktop->target].y;
      }
    } else if(desktop->state == STATE_RESIZING) {
      if(ch == TW_KEY_ESC || ch == 27) { // esc
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if(ch == TW_KEY_ENTER || ch == 10 || ch == 13) {
        window_resize_event_t ev = {desktop->ow - desktop->windows[desktop->target].w,
                                    desktop->oh - desktop->windows[desktop->target].h};
        desktop->windows[desktop->target].w = desktop->ow;
        desktop->windows[desktop->target].h = desktop->oh;
        if(desktop->windows[desktop->target].onevent)
          desktop->windows[desktop->target].onevent(&desktop->windows[desktop->target], desktop,
                                                    WINDOW_EVENT_RESIZE, &ev);
        desktop->state = STATE_NORMAL;
        set_status(desktop, "%d window(s)", desktop->window_count);
      } else if((ch == 'h' || ch == TW_KEY_LEFT) && desktop->ow > 1) --desktop->ow;
      else if(ch == 'l' || ch == TW_KEY_RIGHT) ++desktop->ow;
      else if((ch == 'k' || ch == TW_KEY_UP) && desktop->oh > 1) --desktop->oh;
      else if(ch == 'j' || ch == TW_KEY_DOWN) ++desktop->oh;
    }
  }
  if(desktop->window_count > 0) {
    pthread_t* threads = calloc(desktop->window_count, sizeof(pthread_t));
    window_thread_arg_t* args = calloc(desktop->window_count, sizeof(window_thread_arg_t));
    for(int i = 0; i < desktop->window_count; ++i) {
      args[i].window = &desktop->windows[i];
      args[i].desktop = desktop;
      args[i].index = i;
      pthread_create(&threads[i], NULL, window_update_wrapper, &args[i]);
    }
    for(int i = 0; i < desktop->window_count; ++i) pthread_join(threads[i], NULL);
    free(threads);
    free(args);
  }
  if(desktop->after_update && desktop->after_update(desktop)) return true;
  return false;
}
void desktop_draw(desktop_t* desktop) {
  if(desktop->before_draw) desktop->before_draw(desktop);
  tw_wh_t size = tw_get_size();
  tw_clear(0b01000000);
  tw_fill(0, size.h - 1, size.w, 1, 0b01110000);
  tw_puts(desktop->statustext, 0, size.h - 1, 0b01110000);
  if(desktop->window_count > 0) {
    pthread_t* threads = calloc(desktop->window_count, sizeof(pthread_t));
    window_thread_arg_t* args = calloc(desktop->window_count, sizeof(window_thread_arg_t));
    for(int i = desktop->window_count - 1; i >= 0; --i) {
      args[i].window = &desktop->windows[i];
      args[i].desktop = desktop;
      args[i].index = i;
      pthread_create(&threads[i], NULL, window_draw_wrapper, &args[i]);
    }
    for(int i = desktop->window_count - 1; i >= 0; --i) pthread_join(threads[i], NULL);
    free(threads);
    free(args);
  }
  if(desktop->after_draw) desktop->after_draw(desktop);
}

int main() {
  tw_init();
  desktop_t desktop = {
    .windows = NULL,
    .window_count = 0,
    .window_capacity = 0,
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
  for(int i = 0; i < desktop.window_count; ++i) {
    if(desktop.windows[i].onevent) {
      if(desktop.windows[i].onevent(&desktop.windows[i], &desktop, WINDOW_EVENT_CLOSE, NULL))
        desktop.windows[i].onevent(&desktop.windows[i], &desktop, WINDOW_EVENT_CLOSE_FORCE, NULL);
    }
  }
  if(desktop.windows) free(desktop.windows);
  if(desktop.statustext) free(desktop.statustext);
  return 0;
}
