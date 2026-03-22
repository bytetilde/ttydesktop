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
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pty.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

typedef enum parser_state_t {
  TERM_STATE_NORMAL,
  TERM_STATE_ESCAPE,
  TERM_STATE_CSI,
  TERM_STATE_OSC,
  TERM_STATE_CHARSET
} parser_state_t;
typedef struct term_state_t {
  int master_fd;
  pid_t pid;
  int cx, cy;
  unsigned char fg, bg;
  bool bold, inverse;
  parser_state_t state;
  int params[16];
  int param_count;
  int saved_cx, saved_cy;
  bool is_extended;
  bool show_cursor_mode;
  pthread_t tid;
  pthread_mutex_t mutex;
  bool exited;
  bool cursor_visible;
  int cursor_x, cursor_y;
  short saved_cell;
} term_state_t;

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  term_state_t* ts = window->data;
  if(!ts) return false;
  if(event == WINDOW_EVENT_CLOSE || event == WINDOW_EVENT_CLOSE_FORCE) {
    if(ts->master_fd >= 0) {
      int fd = ts->master_fd;
      ts->master_fd = -1;
      close(fd);
    }
    if(ts->pid > 0) {
      kill(ts->pid, SIGHUP);
      waitpid(ts->pid, NULL, WNOHANG);
    }
    if(ts->tid) pthread_join(ts->tid, NULL);
    pthread_mutex_destroy(&ts->mutex);
    free(ts);
    free(window->title);
    free(window->content);
    window->data = NULL;
    window->title = NULL;
    window->content = NULL;
    return false;
  } else if(event == WINDOW_EVENT_RESIZE) {
    window_resize_event_t* ev = data;
    (void)ev;
    short* tmp = realloc(window->content, window->w * window->h * sizeof(short));
    if(!tmp) return false;
    window->content = tmp;
    struct winsize ws = {.ws_row = window->h, .ws_col = window->w, .ws_xpixel = 0, .ws_ypixel = 0};
    ioctl(ts->master_fd, TIOCSWINSZ, &ws);
  } else if(event == WINDOW_EVENT_KEY) {
    long key = (long)data;
    if(key & TW_MOD_CTRL) {
      int base = key & 0xFFFF;
      char c = 0;
      if(base >= 'a' && base <= 'z') c = base - 'a' + 1;
      else if(base >= 'A' && base <= 'Z') c = base - 'A' + 1;
      else if(base >= '@' && base <= '_') c = base - '@';
      if(c > 0) write(ts->master_fd, &c, 1);
    } else if(key > 0 && key < 128 && key != TW_KEY_UP && key != TW_KEY_DOWN &&
              key != TW_KEY_LEFT && key != TW_KEY_RIGHT && key != TW_KEY_ESC &&
              key != TW_KEY_ENTER && key != TW_KEY_BACKSPACE) {
      char c = (char)key;
      if(key == 10 || key == 13) c = '\r';
      else if(key == 8 || key == 127) c = '\177';
      write(ts->master_fd, &c, 1);
    } else if(key == TW_KEY_ENTER) write(ts->master_fd, "\r", 1);
    else if(key == TW_KEY_BACKSPACE) write(ts->master_fd, "\177", 1);
    else if(key == TW_KEY_UP) write(ts->master_fd, "\033[A", 3);
    else if(key == TW_KEY_DOWN) write(ts->master_fd, "\033[B", 3);
    else if(key == TW_KEY_RIGHT) write(ts->master_fd, "\033[C", 3);
    else if(key == TW_KEY_LEFT) write(ts->master_fd, "\033[D", 3);
    else if(key == TW_KEY_ESC) write(ts->master_fd, "\033", 1);
  }
  return false;
}
void* term_read_thread(void* arg) {
  window_t* window = arg;
  term_state_t* ts = window->data;
  char buf[4096];
  while(1) {
    int fd = ts->master_fd;
    if(fd < 0) break;
    int n = read(fd, buf, sizeof(buf));
    if(n <= 0) {
      if(n == -1 && (errno == EAGAIN || errno == EINTR)) continue;
      ts->exited = true;
      break;
    }
    pthread_mutex_lock(&ts->mutex);
    if(ts->cursor_visible) {
      if(ts->cursor_y >= 0 && ts->cursor_y < window->h && ts->cursor_x >= 0 &&
         ts->cursor_x < window->w) {
        window->content[ts->cursor_y * window->w + ts->cursor_x] = ts->saved_cell;
      }
      ts->cursor_visible = false;
    }
    for(int i = 0; i < n; ++i) {
      char c = buf[i];
      if(ts->state == TERM_STATE_NORMAL) {
        if(c == 27) ts->state = TERM_STATE_ESCAPE;
        else if(c == '\r') ts->cx = 0;
        else if(c == '\n') ++ts->cy;
        else if(c == '\b') {
          if(ts->cx > 0) --ts->cx;
        } else if(c == '\t') ts->cx = (ts->cx + 8) & ~7;
        else if(c == '\a')
          ; // who's bell? never heard of him
        else if(isprint(c)) {
          if(ts->cx >= window->w) {
            ts->cx = 0;
            ++ts->cy;
          }
          unsigned char eff_fg = ts->fg;
          unsigned char eff_bg = ts->bg;
          if(ts->bold) eff_fg |= 0x08;
          if(ts->inverse) {
            unsigned char tmp = eff_fg;
            eff_fg = eff_bg;
            eff_bg = tmp;
          }
          unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
          if(ts->cy >= window->h) {
            // simple scroll up
            // i cant be bothered to implement a scrollback buffer
            memmove(window->content, window->content + window->w,
                    window->w * (window->h - 1) * sizeof(short));
            for(int cx = 0; cx < window->w; ++cx)
              window->content[(window->h - 1) * window->w + cx] = (attr << 8) | ' ';
            ts->cy = window->h - 1;
          }
          if(ts->cy >= 0 && ts->cy < window->h && ts->cx >= 0 && ts->cx < window->w)
            window->content[ts->cy * window->w + ts->cx] = (attr << 8) | c;
          ++ts->cx;
        }
      } else if(ts->state == TERM_STATE_ESCAPE) {
        if(c == '[') {
          ts->state = TERM_STATE_CSI;
          ts->param_count = 0;
          ts->is_extended = false;
          for(int p = 0; p < 16; ++p) ts->params[p] = 0;
        } else if(c == ']') {
          ts->state = TERM_STATE_OSC;
        } else if(c == '(' || c == ')' || c == '*' || c == '+') {
          ts->state = TERM_STATE_CHARSET;
        } else if(c == '7') {
          ts->saved_cx = ts->cx;
          ts->saved_cy = ts->cy;
          ts->state = TERM_STATE_NORMAL;
        } else if(c == '8') {
          ts->cx = ts->saved_cx;
          ts->cy = ts->saved_cy;
          ts->state = TERM_STATE_NORMAL;
        } else ts->state = TERM_STATE_NORMAL;
      } else if(ts->state == TERM_STATE_CHARSET) {
        ts->state = TERM_STATE_NORMAL;
      } else if(ts->state == TERM_STATE_OSC) {
        if(c == '\007') ts->state = TERM_STATE_NORMAL;
        else if(c == '\033') ts->state = TERM_STATE_ESCAPE;
      } else if(ts->state == TERM_STATE_CSI) {
        if(c >= '0' && c <= '9')
          ts->params[ts->param_count] = ts->params[ts->param_count] * 10 + (c - '0');
        else if(c == ';') {
          if(ts->param_count < 15) ++ts->param_count;
        } else if(c == '?') {
          ts->is_extended = true;
        } else {
          int p1 = ts->params[0] == 0 ? 1 : ts->params[0];
          int p2 = ts->params[1] == 0 ? 1 : ts->params[1];
          if(ts->is_extended) {
            if(c == 'h') {
              if(ts->params[0] == 25) ts->show_cursor_mode = true;
            } else if(c == 'l') {
              if(ts->params[0] == 25) ts->show_cursor_mode = false;
            }
          } else {
            switch(c) {
              case 'A':
                ts->cy -= p1;
                if(ts->cy < 0) ts->cy = 0;
                break;
              case 'B':
                ts->cy += p1;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                break;
              case 'C':
                ts->cx += p1;
                if(ts->cx >= window->w) ts->cx = window->w - 1;
                break;
              case 'D':
                ts->cx -= p1;
                if(ts->cx < 0) ts->cx = 0;
                break;
              case 'G':
              case '`':
                ts->cx = p1 - 1;
                if(ts->cx < 0) ts->cx = 0;
                if(ts->cx >= window->w) ts->cx = window->w - 1;
                break;
              case 'd':
                ts->cy = p1 - 1;
                if(ts->cy < 0) ts->cy = 0;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                break;
              case 'e':
                ts->cy += p1;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                break;
              case 'a':
                ts->cx += p1;
                if(ts->cx >= window->w) ts->cx = window->w - 1;
                break;
              case 'H':
              case 'f':
                ts->cy = p1 - 1;
                ts->cx = p2 - 1;
                if(ts->cy < 0) ts->cy = 0;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                if(ts->cx < 0) ts->cx = 0;
                if(ts->cx >= window->w) ts->cx = window->w - 1;
                break;
              case '@': {
                int n = p1;
                if(ts->cx + n < window->w) {
                  memmove(window->content + ts->cy * window->w + ts->cx + n,
                          window->content + ts->cy * window->w + ts->cx,
                          (window->w - ts->cx - n) * sizeof(short));
                }
                unsigned char eff_fg = ts->fg;
                unsigned char eff_bg = ts->bg;
                if(ts->bold) eff_fg |= 0x08;
                if(ts->inverse) {
                  unsigned char tmp = eff_fg;
                  eff_fg = eff_bg;
                  eff_bg = tmp;
                }
                unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                for(int i = 0; i < n && ts->cx + i < window->w; ++i)
                  window->content[ts->cy * window->w + ts->cx + i] = (attr << 8) | ' ';
              } break;
              case 'P': {
                int n = p1;
                if(ts->cx + n < window->w) {
                  memmove(window->content + ts->cy * window->w + ts->cx,
                          window->content + ts->cy * window->w + ts->cx + n,
                          (window->w - ts->cx - n) * sizeof(short));
                }
                unsigned char eff_fg = ts->fg;
                unsigned char eff_bg = ts->bg;
                if(ts->bold) eff_fg |= 0x08;
                if(ts->inverse) {
                  unsigned char tmp = eff_fg;
                  eff_fg = eff_bg;
                  eff_bg = tmp;
                }
                unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                for(int i = 0; i < n && window->w - 1 - i >= ts->cx; ++i)
                  window->content[ts->cy * window->w + window->w - 1 - i] = (attr << 8) | ' ';
              } break;
              case 'X': {
                int n = p1;
                unsigned char eff_fg = ts->fg;
                unsigned char eff_bg = ts->bg;
                if(ts->bold) eff_fg |= 0x08;
                if(ts->inverse) {
                  unsigned char tmp = eff_fg;
                  eff_fg = eff_bg;
                  eff_bg = tmp;
                }
                unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                for(int i = 0; i < n && ts->cx + i < window->w; ++i)
                  window->content[ts->cy * window->w + ts->cx + i] = (attr << 8) | ' ';
              } break;
              case 'J': {
                unsigned char eff_fg = ts->fg;
                unsigned char eff_bg = ts->bg;
                if(ts->bold) eff_fg |= 0x08;
                if(ts->inverse) {
                  unsigned char tmp = eff_fg;
                  eff_fg = eff_bg;
                  eff_bg = tmp;
                }
                unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                int clear_type = ts->params[0];
                if(clear_type == 0) {
                  for(int j = ts->cy; j < window->h; ++j) {
                    int start = (j == ts->cy) ? ts->cx : 0;
                    for(int i = start; i < window->w; ++i)
                      window->content[j * window->w + i] = (attr << 8) | ' ';
                  }
                } else if(clear_type == 1) {
                  for(int j = 0; j <= ts->cy; ++j) {
                    int end = (j == ts->cy) ? ts->cx : window->w - 1;
                    for(int i = 0; i <= end; ++i)
                      window->content[j * window->w + i] = (attr << 8) | ' ';
                  }
                } else if(clear_type == 2) {
                  for(int i = 0; i < window->w * window->h; ++i)
                    window->content[i] = (attr << 8) | ' ';
                }
              } break;
              case 'K': {
                unsigned char eff_fg = ts->fg;
                unsigned char eff_bg = ts->bg;
                if(ts->bold) eff_fg |= 0x08;
                if(ts->inverse) {
                  unsigned char tmp = eff_fg;
                  eff_fg = eff_bg;
                  eff_bg = tmp;
                }
                unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                int clear_type = ts->params[0];
                if(clear_type == 0)
                  for(int i = ts->cx; i < window->w; ++i)
                    window->content[ts->cy * window->w + i] = (attr << 8) | ' ';
                else if(clear_type == 1)
                  for(int i = 0; i <= ts->cx; ++i)
                    window->content[ts->cy * window->w + i] = (attr << 8) | ' ';
                else if(clear_type == 2)
                  for(int i = 0; i < window->w; ++i)
                    window->content[ts->cy * window->w + i] = (attr << 8) | ' ';
              } break;
              case 'L': {
                unsigned char eff_fg = ts->fg;
                unsigned char eff_bg = ts->bg;
                if(ts->bold) eff_fg |= 0x08;
                if(ts->inverse) {
                  unsigned char tmp = eff_fg;
                  eff_fg = eff_bg;
                  eff_bg = tmp;
                }
                unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                int n = p1;
                if(ts->cy < window->h) {
                  int move_count = window->h - ts->cy - n;
                  if(move_count > 0)
                    memmove(window->content + (ts->cy + n) * window->w,
                            window->content + ts->cy * window->w,
                            move_count * window->w * sizeof(short));
                  for(int i = 0; i < n && ts->cy + i < window->h; ++i)
                    for(int c = 0; c < window->w; ++c)
                      window->content[(ts->cy + i) * window->w + c] = (attr << 8) | ' ';
                }
              } break;
              case 'M': {
                unsigned char eff_fg = ts->fg;
                unsigned char eff_bg = ts->bg;
                if(ts->bold) eff_fg |= 0x08;
                if(ts->inverse) {
                  unsigned char tmp = eff_fg;
                  eff_fg = eff_bg;
                  eff_bg = tmp;
                }
                unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                int n = p1;
                if(ts->cy < window->h) {
                  int move_count = window->h - ts->cy - n;
                  if(move_count > 0)
                    memmove(window->content + ts->cy * window->w,
                            window->content + (ts->cy + n) * window->w,
                            move_count * window->w * sizeof(short));
                  for(int i = 0; i < n && window->h - 1 - i >= ts->cy; ++i)
                    for(int c = 0; c < window->w; ++c)
                      window->content[(window->h - 1 - i) * window->w + c] = (attr << 8) | ' ';
                }
              } break;
              case 'S': {
                int n = p1;
                if(n < window->h) {
                  memmove(window->content, window->content + n * window->w,
                          (window->h - n) * window->w * sizeof(short));
                  unsigned char eff_fg = ts->fg;
                  unsigned char eff_bg = ts->bg;
                  if(ts->bold) eff_fg |= 0x08;
                  if(ts->inverse) {
                    unsigned char tmp = eff_fg;
                    eff_fg = eff_bg;
                    eff_bg = tmp;
                  }
                  unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                  for(int i = (window->h - n) * window->w; i < window->w * window->h; ++i)
                    window->content[i] = (attr << 8) | ' ';
                } else {
                  unsigned char eff_fg = ts->fg;
                  unsigned char eff_bg = ts->bg;
                  if(ts->bold) eff_fg |= 0x08;
                  if(ts->inverse) {
                    unsigned char tmp = eff_fg;
                    eff_fg = eff_bg;
                    eff_bg = tmp;
                  }
                  unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                  for(int i = 0; i < window->w * window->h; ++i)
                    window->content[i] = (attr << 8) | ' ';
                }
              } break;
              case 'T': {
                int n = p1;
                if(n < window->h) {
                  memmove(window->content + n * window->w, window->content,
                          (window->h - n) * window->w * sizeof(short));
                  unsigned char eff_fg = ts->fg;
                  unsigned char eff_bg = ts->bg;
                  if(ts->bold) eff_fg |= 0x08;
                  if(ts->inverse) {
                    unsigned char tmp = eff_fg;
                    eff_fg = eff_bg;
                    eff_bg = tmp;
                  }
                  unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                  for(int i = 0; i < n * window->w; ++i) window->content[i] = (attr << 8) | ' ';
                } else {
                  unsigned char eff_fg = ts->fg;
                  unsigned char eff_bg = ts->bg;
                  if(ts->bold) eff_fg |= 0x08;
                  if(ts->inverse) {
                    unsigned char tmp = eff_fg;
                    eff_fg = eff_bg;
                    eff_bg = tmp;
                  }
                  unsigned char attr = (eff_bg << 4) | (eff_fg & 0x0F);
                  for(int i = 0; i < window->w * window->h; ++i)
                    window->content[i] = (attr << 8) | ' ';
                }
              } break;
              case 'm': {
                if(ts->param_count == 0 && ts->params[0] == 0) {
                  ts->fg = 7;
                  ts->bg = 0;
                  ts->bold = false;
                  ts->inverse = false;
                } else {
                  for(int i = 0; i <= ts->param_count; ++i) {
                    int p = ts->params[i];
                    if(p == 0) {
                      ts->fg = 7;
                      ts->bg = 0;
                      ts->bold = false;
                      ts->inverse = false;
                    } else if(p == 1) ts->bold = true;
                    else if(p == 7) ts->inverse = true;
                    else if(p == 22) ts->bold = false;
                    else if(p == 27) ts->inverse = false;
                    else if(p >= 30 && p <= 37) ts->fg = p - 30;
                    else if(p == 39) ts->fg = 7;
                    else if(p >= 40 && p <= 47) ts->bg = p - 40;
                    else if(p == 49) ts->bg = 0;
                  }
                }
              } break;
              case 's':
                ts->saved_cx = ts->cx;
                ts->saved_cy = ts->cy;
                break;
              case 'u':
                ts->cx = ts->saved_cx;
                ts->cy = ts->saved_cy;
                break;
            }
          }
          ts->state = TERM_STATE_NORMAL;
        }
      }
    }
    pthread_mutex_unlock(&ts->mutex);
  }
  return NULL;
}
void update(window_t* window, desktop_t* desktop) {
  (void)desktop;
  term_state_t* ts = window->data;
  if(!ts) return;
  if(ts->exited) {
    window->close_pending = true;
    return;
  }
  pthread_mutex_lock(&ts->mutex);
  if(ts->cursor_visible) {
    if(ts->cursor_y >= 0 && ts->cursor_y < window->h && ts->cursor_x >= 0 &&
       ts->cursor_x < window->w) {
      window->content[ts->cursor_y * window->w + ts->cursor_x] = ts->saved_cell;
    }
  }
  ts->cursor_x = ts->cx;
  ts->cursor_y = ts->cy;
  if(ts->show_cursor_mode && ts->cursor_y >= 0 && ts->cursor_y < window->h && ts->cursor_x >= 0 &&
     ts->cursor_x < window->w) {
    ts->saved_cell = window->content[ts->cursor_y * window->w + ts->cursor_x];
    char c = ts->saved_cell & 0xFF;
    char attr = (ts->saved_cell >> 8) & 0xFF;
    char inv_attr = ((attr & 0x0F) << 4) | ((attr & 0xF0) >> 4);
    if(inv_attr == 0) inv_attr = 0x77;
    window->content[ts->cursor_y * window->w + ts->cursor_x] = (inv_attr << 8) | c;
    ts->cursor_visible = true;
  } else ts->cursor_visible = false;
  pthread_mutex_unlock(&ts->mutex);
}

void window_init(desktop_t* desktop, window_t* win) {
  (void)desktop;
  win->w = 80;
  win->h = 24;
  win->title = strdup("terminal");
  win->content = calloc(win->w * win->h, sizeof(short));
  if(!win->content) {
    free(win->title);
    win->title = NULL;
    win->close_pending = true;
    return;
  }
  term_state_t* ts = calloc(1, sizeof(term_state_t));
  if(!ts) {
    free(win->content);
    free(win->title);
    win->title = NULL;
    win->close_pending = true;
    return;
  }
  win->data = ts;
  struct winsize ws = {.ws_row = win->h, .ws_col = win->w, .ws_xpixel = 0, .ws_ypixel = 0};
  pid_t pid = forkpty(&ts->master_fd, NULL, NULL, &ws);
  if(pid == -1) {
    free(ts);
    free(win->content);
    free(win->title);
    win->title = NULL;
    win->close_pending = true;
    return;
  } else if(pid == 0) {
    setenv("TERM", "xterm-256color", 1); // get your expectations high
    const char* shell = getenv("SHELL");
    if(!shell) shell = "/bin/sh";
    execlp(shell, shell, NULL);
    exit(1);
  }
  ts->pid = pid;
  ts->fg = 7;
  ts->bg = 0;
  ts->bold = false;
  ts->inverse = false;
  ts->show_cursor_mode = true;
  win->onevent = onevent;
  win->update = update;
  pthread_mutex_init(&ts->mutex, NULL);
  pthread_create(&ts->tid, NULL, term_read_thread, win);
}
