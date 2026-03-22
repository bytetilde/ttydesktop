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
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <pty.h>
#include <stdio.h>
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
  TERM_STATE_CHARSET,
  TERM_STATE_SS3
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
  bool unfocus_pending;
  short* content_backup;
  int backup_size;
  int scroll_top, scroll_bot;
  bool app_cursor_keys;
  bool auto_wrap;
  bool using_alt_screen;
  short* alt_screen;
  int saved_cx_main, saved_cy_main;
  char osc_buf[256];
  int osc_len;
  int osc_param;
  int sgr_pending;
  int sgr_sub;
} term_state_t;

static inline unsigned char make_attr(term_state_t* ts) {
  unsigned char fg = ts->fg;
  unsigned char bg = ts->bg;
  if(ts->bold) fg |= 0x08;
  if(ts->inverse) {
    unsigned char t = fg;
    fg = bg;
    bg = t;
  }
  return (bg << 4) | (fg & 0x0F);
}
static void fill_cells(short* content, int start, int count, unsigned char attr) {
  for(int i = 0; i < count; ++i) content[start + i] = (attr << 8) | ' ';
}
static void scroll_up(window_t* w, term_state_t* ts, int n) {
  unsigned char attr = make_attr(ts);
  int top = ts->scroll_top;
  int bot = ts->scroll_bot;
  int region_h = bot - top + 1;
  if(n >= region_h) {
    fill_cells(w->content, top * w->w, region_h * w->w, attr);
    return;
  }
  memmove(w->content + top * w->w, w->content + (top + n) * w->w,
          (region_h - n) * w->w * sizeof(short));
  fill_cells(w->content, (bot - n + 1) * w->w, n * w->w, attr);
}
static void scroll_down(window_t* w, term_state_t* ts, int n) {
  unsigned char attr = make_attr(ts);
  int top = ts->scroll_top;
  int bot = ts->scroll_bot;
  int region_h = bot - top + 1;
  if(n >= region_h) {
    fill_cells(w->content, top * w->w, region_h * w->w, attr);
    return;
  }
  memmove(w->content + (top + n) * w->w, w->content + top * w->w,
          (region_h - n) * w->w * sizeof(short));
  fill_cells(w->content, top * w->w, n * w->w, attr);
}
static void cursor_newline(window_t* w, term_state_t* ts) {
  if(ts->cy == ts->scroll_bot) {
    scroll_up(w, ts, 1);
  } else {
    ++ts->cy;
    if(ts->cy >= w->h) ts->cy = w->h - 1;
  }
}

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
    free(ts->content_backup);
    free(ts->alt_screen);
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
    int new_sz = window->w * window->h;
    short* tmp = realloc(window->content, new_sz * sizeof(short));
    if(!tmp) return false;
    window->content = tmp;
    if(ts->alt_screen) {
      short* atmp = realloc(ts->alt_screen, new_sz * sizeof(short));
      if(atmp) ts->alt_screen = atmp;
    }
    if(ts->cx >= window->w) ts->cx = window->w - 1;
    if(ts->cy >= window->h) ts->cy = window->h - 1;
    if(ts->saved_cx >= window->w) ts->saved_cx = window->w - 1;
    if(ts->saved_cy >= window->h) ts->saved_cy = window->h - 1;
    if(ts->scroll_bot >= window->h) ts->scroll_bot = window->h - 1;
    if(ts->scroll_top > ts->scroll_bot) ts->scroll_top = 0;
    struct winsize ws = {.ws_row = window->h, .ws_col = window->w, .ws_xpixel = 0, .ws_ypixel = 0};
    ioctl(ts->master_fd, TIOCSWINSZ, &ws);
  } else if(event == WINDOW_EVENT_KEY) {
    long key = (long)data;
    if(ts->unfocus_pending) {
      if(key == TW_KEY_ENTER) write(ts->master_fd, "\033", 1);
      ts->unfocus_pending = false;
      return false;
    }
    int fd = ts->master_fd;
    if(fd < 0) return false;
    bool alt = (key & TW_MOD_ALT) != 0;
    bool ctrl = (key & TW_MOD_CTRL) != 0;
    int base = key & 0xFFFF;
    if(ctrl) {
      char c = -1;
      if(base >= 'a' && base <= 'z') c = base - 'a' + 1;
      else if(base >= 'A' && base <= 'Z') c = base - 'A' + 1;
      else if(base >= '@' && base <= '_') c = base - '@';
      if(c >= 0) {
        if(alt) write(fd, "\033", 1);
        write(fd, &c, 1);
      }
      return false;
    }
#define SEND(seq)                                                                                  \
  do {                                                                                             \
    if(alt) write(fd, "\033", 1);                                                                  \
    write(fd, (seq), sizeof(seq) - 1);                                                             \
  } while(0)
    if(base == TW_KEY_UP) SEND(ts->app_cursor_keys ? "\033OA" : "\033[A");
    else if(base == TW_KEY_DOWN) SEND(ts->app_cursor_keys ? "\033OB" : "\033[B");
    else if(base == TW_KEY_RIGHT) SEND(ts->app_cursor_keys ? "\033OC" : "\033[C");
    else if(base == TW_KEY_LEFT) SEND(ts->app_cursor_keys ? "\033OD" : "\033[D");
    else if(base == TW_KEY_ENTER) SEND("\r");
    else if(base == TW_KEY_BACKSPACE) SEND("\177");
    else if(base == TW_KEY_ESC) SEND("\033");
    else if(base == TW_KEY_TAB) SEND("\t");
    else if(base == TW_KEY_HOME) SEND("\033[1~");
    else if(base == TW_KEY_END) SEND("\033[4~");
    else if(base == TW_KEY_INSERT) SEND("\033[2~");
    else if(base == TW_KEY_DELETE) SEND("\033[3~");
    else if(base == TW_KEY_PAGE_UP) SEND("\033[5~");
    else if(base == TW_KEY_PAGE_DOWN) SEND("\033[6~");
    else if(base == TW_KEY_F1) SEND("\033OP");
    else if(base == TW_KEY_F2) SEND("\033OQ");
    else if(base == TW_KEY_F3) SEND("\033OR");
    else if(base == TW_KEY_F4) SEND("\033OS");
    else if(base == TW_KEY_F5) SEND("\033[15~");
    else if(base == TW_KEY_F6) SEND("\033[17~");
    else if(base == TW_KEY_F7) SEND("\033[18~");
    else if(base == TW_KEY_F8) SEND("\033[19~");
    else if(base == TW_KEY_F9) SEND("\033[20~");
    else if(base == TW_KEY_F10) SEND("\033[21~");
    else if(base == TW_KEY_F11) SEND("\033[23~");
    else if(base == TW_KEY_F12) SEND("\033[24~");
    else if(base > 0 && base < 128) {
      char c = (char)base;
      if(alt) write(fd, "\033", 1);
      write(fd, &c, 1);
    }
#undef SEND
  } else if(event == WINDOW_EVENT_UNFOCUS) {
    ts->unfocus_pending = !ts->unfocus_pending;
    return ts->unfocus_pending;
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
      unsigned char c = (unsigned char)buf[i];
      if(ts->state == TERM_STATE_NORMAL) {
        if(c == 0x84) {
          cursor_newline(window, ts);
          continue;
        } else if(c == 0x85) {
          ts->cx = 0;
          cursor_newline(window, ts);
          continue;
        } else if(c == 0x8D) {
          if(ts->cy == ts->scroll_top) scroll_down(window, ts, 1);
          else if(ts->cy > 0) --ts->cy;
          continue;
        } else if(c == 0x9B) {
          ts->state = TERM_STATE_CSI;
          ts->param_count = 0;
          ts->is_extended = false;
          for(int p = 0; p < 16; ++p) ts->params[p] = 0;
          continue;
        } else if(c >= 0x80) continue;
        if(c == 27) ts->state = TERM_STATE_ESCAPE;
        else if(c == '\r') ts->cx = 0;
        else if(c == '\n' || c == '\v' || c == '\f') cursor_newline(window, ts);
        else if(c == '\b') {
          if(ts->cx > 0) --ts->cx;
        } else if(c == '\t') {
          ts->cx = (ts->cx + 8) & ~7;
          if(ts->cx >= window->w) ts->cx = window->w - 1;
        } else if(c == '\a')
          ; // who's bell? never heard of him
        else if(c == '\x0E' || c == '\x0F')
          ; // SO/SI
        else if(c == '\x18' || c == '\x1A') ts->state = TERM_STATE_NORMAL;
        else if(c >= 0x20) {
          if(ts->cx >= window->w) {
            if(ts->auto_wrap) {
              ts->cx = 0;
              cursor_newline(window, ts);
            } else ts->cx = window->w - 1;
          }
          unsigned char attr = make_attr(ts);
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
          ts->osc_len = 0;
          ts->osc_param = -1;
          ts->osc_buf[0] = '\0';
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
        } else if(c == 'M') {
          if(ts->cy == ts->scroll_top) scroll_down(window, ts, 1);
          else if(ts->cy > 0) --ts->cy;
          ts->state = TERM_STATE_NORMAL;
        } else if(c == 'D') {
          cursor_newline(window, ts);
          ts->state = TERM_STATE_NORMAL;
        } else if(c == 'E') {
          ts->cx = 0;
          cursor_newline(window, ts);
          ts->state = TERM_STATE_NORMAL;
        } else if(c == 'c') {
          ts->cx = 0;
          ts->cy = 0;
          ts->saved_cx = 0;
          ts->saved_cy = 0;
          ts->fg = 7;
          ts->bg = 0;
          ts->bold = false;
          ts->inverse = false;
          ts->auto_wrap = true;
          ts->app_cursor_keys = false;
          ts->show_cursor_mode = true;
          ts->scroll_top = 0;
          ts->scroll_bot = window->h - 1;
          fill_cells(window->content, 0, window->w * window->h, 0x07);
          ts->state = TERM_STATE_NORMAL;
        } else if(c == 'O') ts->state = TERM_STATE_SS3;
        else if(c == '=' || c == '>') ts->state = TERM_STATE_NORMAL;
        else if(c == '\\') ts->state = TERM_STATE_NORMAL;
        else if(c == '\x18' || c == '\x1A') ts->state = TERM_STATE_NORMAL;
        else ts->state = TERM_STATE_NORMAL;
      } else if(ts->state == TERM_STATE_SS3) {
        if(c == 'A') {
          if(ts->cy > 0) --ts->cy;
        } else if(c == 'B') {
          if(ts->cy < window->h - 1) ++ts->cy;
        } else if(c == 'C') {
          if(ts->cx < window->w - 1) ++ts->cx;
        } else if(c == 'D') {
          if(ts->cx > 0) --ts->cx;
        } else if(c == 'H') {
          ts->cx = 0;
          ts->cy = 0;
        } else if(c == 'F') {
          ts->cx = window->w - 1;
          ts->cy = window->h - 1;
        }
        ts->state = TERM_STATE_NORMAL;
      } else if(ts->state == TERM_STATE_CHARSET) {
        ts->state = TERM_STATE_NORMAL;
      } else if(ts->state == TERM_STATE_OSC) {
        if(c == '\x18' || c == '\x1A') {
          ts->state = TERM_STATE_NORMAL;
        } else if(c == '\007' || c == 0x9C) {
          ts->osc_buf[ts->osc_len] = '\0';
          if((ts->osc_param == 0 || ts->osc_param == 1 || ts->osc_param == 2) && ts->osc_len > 0) {
            free(window->title);
            window->title = strdup(ts->osc_buf);
          }
          ts->state = TERM_STATE_NORMAL;
        } else if(c == '\033') {
          ts->osc_buf[ts->osc_len] = '\0';
          if((ts->osc_param == 0 || ts->osc_param == 1 || ts->osc_param == 2) && ts->osc_len > 0) {
            free(window->title);
            window->title = strdup(ts->osc_buf);
          }
          ts->state = TERM_STATE_ESCAPE;
        } else if(ts->osc_param < 0) {
          if(c == ';') {
            ts->osc_buf[ts->osc_len] = '\0';
            ts->osc_param = atoi(ts->osc_buf);
            ts->osc_len = 0;
          } else if(ts->osc_len < (int)(sizeof(ts->osc_buf) - 1))
            ts->osc_buf[ts->osc_len++] = (char)c;
        } else if(ts->osc_len < (int)(sizeof(ts->osc_buf) - 1))
          ts->osc_buf[ts->osc_len++] = (char)c;
      } else if(ts->state == TERM_STATE_CSI) {
        if(c >= '0' && c <= '9')
          ts->params[ts->param_count] = ts->params[ts->param_count] * 10 + (c - '0');
        else if(c == ';') {
          if(ts->param_count < 15) ++ts->param_count;
        } else if(c == '?') ts->is_extended = true;
        else if(c == '\x18' || c == '\x1A') {
          ts->state = TERM_STATE_NORMAL;
          continue;
        } else {
          int p0 = ts->params[0];
          int p1 = ts->params[1];
          int p1def = (p1 == 0) ? 1 : p1;
          int p0def = (p0 == 0) ? 1 : p0;
          if(ts->is_extended) {
            if(c == 'h' || c == 'l') {
              bool on = (c == 'h');
              for(int pi = 0; pi <= ts->param_count; ++pi) {
                switch(ts->params[pi]) {
                  case 1: ts->app_cursor_keys = on; break;
                  case 7: ts->auto_wrap = on; break;
                  case 12: break;
                  case 25: ts->show_cursor_mode = on; break;
                  case 47:
                    if(on && !ts->using_alt_screen) {
                      int sz = window->w * window->h;
                      ts->alt_screen = malloc(sz * sizeof(short));
                      if(ts->alt_screen) {
                        memcpy(ts->alt_screen, window->content, sz * sizeof(short));
                        ts->using_alt_screen = true;
                      }
                    } else if(!on && ts->using_alt_screen) {
                      int sz = window->w * window->h;
                      memcpy(window->content, ts->alt_screen, sz * sizeof(short));
                      free(ts->alt_screen);
                      ts->alt_screen = NULL;
                      ts->using_alt_screen = false;
                    }
                    break;
                  case 1047:
                    if(on && !ts->using_alt_screen) {
                      int sz = window->w * window->h;
                      ts->alt_screen = malloc(sz * sizeof(short));
                      if(ts->alt_screen) {
                        memcpy(ts->alt_screen, window->content, sz * sizeof(short));
                        fill_cells(window->content, 0, sz, make_attr(ts));
                        ts->using_alt_screen = true;
                      }
                    } else if(!on && ts->using_alt_screen) {
                      int sz = window->w * window->h;
                      memcpy(window->content, ts->alt_screen, sz * sizeof(short));
                      free(ts->alt_screen);
                      ts->alt_screen = NULL;
                      ts->using_alt_screen = false;
                    }
                    break;
                  case 1049:
                    if(on && !ts->using_alt_screen) {
                      ts->saved_cx_main = ts->cx;
                      ts->saved_cy_main = ts->cy;
                      int sz = window->w * window->h;
                      ts->alt_screen = malloc(sz * sizeof(short));
                      if(ts->alt_screen) {
                        memcpy(ts->alt_screen, window->content, sz * sizeof(short));
                        fill_cells(window->content, 0, sz, make_attr(ts));
                        ts->using_alt_screen = true;
                      }
                    } else if(!on && ts->using_alt_screen) {
                      int sz = window->w * window->h;
                      memcpy(window->content, ts->alt_screen, sz * sizeof(short));
                      free(ts->alt_screen);
                      ts->alt_screen = NULL;
                      ts->using_alt_screen = false;
                      ts->cx = ts->saved_cx_main;
                      ts->cy = ts->saved_cy_main;
                      if(ts->cx >= window->w) ts->cx = window->w - 1;
                      if(ts->cy >= window->h) ts->cy = window->h - 1;
                    }
                    break;
                  case 2004: break;
                  default: break;
                }
              }
            } else if(c == 'n') {
              // dsr in private mode cant be bothered
            }
          } else {
            switch(c) {
              case 'A':
                ts->cy -= p0def;
                if(ts->cy < 0) ts->cy = 0;
                break;
              case 'B':
                ts->cy += p0def;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                break;
              case 'C':
              case 'a':
                ts->cx += p0def;
                if(ts->cx >= window->w) ts->cx = window->w - 1;
                break;
              case 'D':
                ts->cx -= p0def;
                if(ts->cx < 0) ts->cx = 0;
                break;
              case 'E':
                ts->cy += p0def;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                ts->cx = 0;
                break;
              case 'F':
                ts->cy -= p0def;
                if(ts->cy < 0) ts->cy = 0;
                ts->cx = 0;
                break;
              case 'G':
              case '`':
                ts->cx = p0def - 1;
                if(ts->cx < 0) ts->cx = 0;
                if(ts->cx >= window->w) ts->cx = window->w - 1;
                break;
              case 'd':
                ts->cy = p0def - 1;
                if(ts->cy < 0) ts->cy = 0;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                break;
              case 'e':
                ts->cy += p0def;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                break;
              case 'H':
              case 'f':
                ts->cy = p0def - 1;
                ts->cx = p1def - 1;
                if(ts->cy < 0) ts->cy = 0;
                if(ts->cy >= window->h) ts->cy = window->h - 1;
                if(ts->cx < 0) ts->cx = 0;
                if(ts->cx >= window->w) ts->cx = window->w - 1;
                break;
              case 'r': {
                int top = (p0 == 0 ? 1 : p0) - 1;
                int bot = (p1 == 0 ? window->h : p1) - 1;
                if(top < 0) top = 0;
                if(bot >= window->h) bot = window->h - 1;
                if(top < bot) {
                  ts->scroll_top = top;
                  ts->scroll_bot = bot;
                  ts->cx = 0;
                  ts->cy = 0;
                }
              } break;
              case 'J': {
                unsigned char attr = make_attr(ts);
                int et = p0;
                if(et == 0) {
                  fill_cells(window->content, ts->cy * window->w + ts->cx, window->w - ts->cx,
                             attr);
                  fill_cells(window->content, (ts->cy + 1) * window->w,
                             (window->h - ts->cy - 1) * window->w, attr);
                } else if(et == 1) {
                  fill_cells(window->content, 0, ts->cy * window->w, attr);
                  fill_cells(window->content, ts->cy * window->w, ts->cx + 1, attr);
                } else if(et == 2 || et == 3) {
                  fill_cells(window->content, 0, window->w * window->h, attr);
                }
              } break;
              case 'K': {
                unsigned char attr = make_attr(ts);
                int et = p0;
                if(et == 0)
                  fill_cells(window->content, ts->cy * window->w + ts->cx, window->w - ts->cx,
                             attr);
                else if(et == 1) fill_cells(window->content, ts->cy * window->w, ts->cx + 1, attr);
                else if(et == 2) fill_cells(window->content, ts->cy * window->w, window->w, attr);
              } break;
              case 'L': {
                unsigned char attr = make_attr(ts);
                int nn = p0def;
                int top = ts->cy;
                int bot = ts->scroll_bot;
                int region_h = bot - top + 1;
                if(top <= bot) {
                  int move = region_h - nn;
                  if(move > 0)
                    memmove(window->content + (top + nn) * window->w,
                            window->content + top * window->w, move * window->w * sizeof(short));
                  int clear = (nn < region_h) ? nn : region_h;
                  fill_cells(window->content, top * window->w, clear * window->w, attr);
                }
              } break;
              case 'M': {
                unsigned char attr = make_attr(ts);
                int nn = p0def;
                int top = ts->cy;
                int bot = ts->scroll_bot;
                int region_h = bot - top + 1;
                if(top <= bot) {
                  int move = region_h - nn;
                  if(move > 0)
                    memmove(window->content + top * window->w,
                            window->content + (top + nn) * window->w,
                            move * window->w * sizeof(short));
                  int clear = (nn < region_h) ? nn : region_h;
                  fill_cells(window->content, (bot - clear + 1) * window->w, clear * window->w,
                             attr);
                }
              } break;
              case '@': {
                unsigned char attr = make_attr(ts);
                int nn = p0def;
                int row = ts->cy * window->w;
                int avail = window->w - ts->cx;
                if(nn > avail) nn = avail;
                memmove(window->content + row + ts->cx + nn, window->content + row + ts->cx,
                        (avail - nn) * sizeof(short));
                fill_cells(window->content, row + ts->cx, nn, attr);
              } break;
              case 'P': {
                unsigned char attr = make_attr(ts);
                int nn = p0def;
                int row = ts->cy * window->w;
                int avail = window->w - ts->cx;
                if(nn > avail) nn = avail;
                memmove(window->content + row + ts->cx, window->content + row + ts->cx + nn,
                        (avail - nn) * sizeof(short));
                fill_cells(window->content, row + window->w - nn, nn, attr);
              } break;
              case 'X': {
                unsigned char attr = make_attr(ts);
                int nn = p0def;
                if(ts->cx + nn > window->w) nn = window->w - ts->cx;
                fill_cells(window->content, ts->cy * window->w + ts->cx, nn, attr);
              } break;
              case 'S': scroll_up(window, ts, p0def); break;
              case 'T': scroll_down(window, ts, p0def); break;
              case 'm': {
                ts->sgr_pending = 0;
                ts->sgr_sub = 0;
                int total = ts->param_count + 1;
                for(int pi = 0; pi < total; ++pi) {
                  int p = ts->params[pi];
                  if(ts->sgr_pending) {
                    if(ts->sgr_sub == 0) {
                      if(p == 5) {
                        ts->sgr_sub = 1;
                        continue;
                      } else if(p == 2) {
                        ts->sgr_sub = 3;
                        continue;
                      } else {
                        ts->sgr_pending = 0;
                        continue;
                      }
                    } else {
                      if(--ts->sgr_sub == 0) ts->sgr_pending = 0;
                      continue;
                    }
                  }
                  if(p == 0) {
                    ts->fg = 7;
                    ts->bg = 0;
                    ts->bold = false;
                    ts->inverse = false;
                  } else if(p == 1) ts->bold = true;
                  else if(p == 2 || p == 3 || p == 4 || p == 21 || p == 23 || p == 24) {
                  } else if(p == 7) ts->inverse = true;
                  else if(p == 22) ts->bold = false;
                  else if(p == 27) ts->inverse = false;
                  else if(p >= 30 && p <= 37) ts->fg = p - 30;
                  else if(p == 38) {
                    ts->sgr_pending = 38;
                    ts->sgr_sub = 0;
                  } else if(p == 39) ts->fg = 7;
                  else if(p >= 40 && p <= 47) ts->bg = p - 40;
                  else if(p == 48) {
                    ts->sgr_pending = 48;
                    ts->sgr_sub = 0;
                  } else if(p == 49) ts->bg = 0;
                  else if(p >= 90 && p <= 97) ts->fg = (p - 90) | 0x08;
                  else if(p >= 100 && p <= 107) ts->bg = (p - 100) | 0x08;
                }
              } break;
              case 'c': {
                if(p0 == 0) write(ts->master_fd, "\033[?1;2c", 7);
              } break;
              case 'n': {
                if(p0 == 5) {
                  write(ts->master_fd, "\033[0n", 4);
                } else if(p0 == 6) {
                  char resp[32];
                  int len = snprintf(resp, sizeof(resp), "\033[%d;%dR", ts->cy + 1, ts->cx + 1);
                  write(ts->master_fd, resp, len);
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
              default: break;
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
    ts->cursor_visible = false;
  }
  if(ts->unfocus_pending) {
    if(!ts->content_backup) {
      ts->content_backup = malloc(window->w * window->h * sizeof(short));
      if(ts->content_backup) {
        memcpy(ts->content_backup, window->content, window->w * window->h * sizeof(short));
        ts->backup_size = window->w * window->h;
      }
    }
    const char line1[13] __attribute__((nonstring)) = "[esc] unfocus";
    const char line2[16] __attribute__((nonstring)) = "[enter] send esc";
    if(window->w >= 13 && window->h >= 1)
      for(int j = 0; j < 13; ++j)
        window->content[(window->w - 13 + j)] = (0b01110000 << 8) | line1[j];
    if(window->w >= 16 && window->h >= 2)
      for(int j = 0; j < 16; ++j)
        window->content[window->w + (window->w - 16 + j)] = (0b01110000 << 8) | line2[j];
  } else if(ts->content_backup) {
    if(ts->backup_size == window->w * window->h)
      memcpy(window->content, ts->content_backup, ts->backup_size * sizeof(short));
    free(ts->content_backup);
    ts->content_backup = NULL;
    ts->backup_size = 0;
  }
  ts->cursor_x = ts->cx;
  ts->cursor_y = ts->cy;
  if(ts->show_cursor_mode && ts->cursor_y >= 0 && ts->cursor_y < window->h && ts->cursor_x >= 0 &&
     ts->cursor_x < window->w) {
    ts->saved_cell = window->content[ts->cursor_y * window->w + ts->cursor_x];
    char c = ts->saved_cell & 0xFF;
    char attr = (ts->saved_cell >> 8) & 0xFF;
    char inv = ((attr & 0x0F) << 4) | ((attr & 0xF0) >> 4);
    if(inv == 0) inv = 0x70;
    window->content[ts->cursor_y * window->w + ts->cursor_x] = (inv << 8) | c;
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
    _exit(1);
  }
  ts->pid = pid;
  ts->fg = 7;
  ts->bg = 0;
  ts->bold = false;
  ts->inverse = false;
  ts->show_cursor_mode = true;
  ts->auto_wrap = true;
  ts->app_cursor_keys = false;
  ts->scroll_top = 0;
  ts->scroll_bot = win->h - 1;
  ts->osc_param = -1;
  win->onevent = onevent;
  win->update = update;
  pthread_mutex_init(&ts->mutex, NULL);
  pthread_create(&ts->tid, NULL, term_read_thread, win);
}
