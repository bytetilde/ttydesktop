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
#include <fcntl.h>
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
  TERM_STATE_OSC
} parser_state_t;
typedef struct term_state_t {
  int master_fd;
  pid_t pid;
  int cx, cy;
  char current_attr;
  enum parser_state_t state;
  int params[16];
  int param_count;
  int saved_cx, saved_cy;
} term_state_t;

bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  term_state_t* ts = window->data;
  if(!ts) return false;
  if(event == WINDOW_EVENT_CLOSE) {
    kill(ts->pid, SIGHUP);
    close(ts->master_fd);
    waitpid(ts->pid, NULL, WNOHANG);
    free(ts);
    free(window->title);
    free(window->content);
    window->data = NULL;
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
    if(key >= 32 && key <= 126) {
      char c = (char)key;
      write(ts->master_fd, &c, 1);
    } else if(key == TW_KEY_ENTER || key == 10 || key == 13) write(ts->master_fd, "\r", 1);
    else if(key == TW_KEY_BACKSPACE || key == 8 || key == 127) write(ts->master_fd, "\177", 1);
    else if(key == TW_KEY_UP) write(ts->master_fd, "\033[A", 3);
    else if(key == TW_KEY_DOWN) write(ts->master_fd, "\033[B", 3);
    else if(key == TW_KEY_RIGHT) write(ts->master_fd, "\033[C", 3);
    else if(key == TW_KEY_LEFT) write(ts->master_fd, "\033[D", 3);
    else if(key == TW_KEY_ESC || key == 27) write(ts->master_fd, "\033", 1);
  }
  return false;
}
void update(window_t* window, desktop_t* desktop) {
  (void)desktop;
  term_state_t* ts = window->data;
  if(!ts) return;
  char buf[256];
  int n = read(ts->master_fd, buf, sizeof(buf));
  if(n > 0) {
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
          if(ts->cy >= window->h) {
            // simple scroll up
            // i cant be bothered to implement a scrollback buffer
            memmove(window->content, window->content + window->w,
                    window->w * (window->h - 1) * sizeof(short));
            for(int cx = 0; cx < window->w; ++cx)
              window->content[(window->h - 1) * window->w + cx] = ts->current_attr << 8 | ' ';
            ts->cy = window->h - 1;
          }
          if(ts->cy >= 0 && ts->cy < window->h && ts->cx >= 0 && ts->cx < window->w)
            window->content[ts->cy * window->w + ts->cx] = (ts->current_attr << 8) | c;
          ++ts->cx;
        }
      } else if(ts->state == TERM_STATE_ESCAPE) {
        if(c == '[') {
          ts->state = TERM_STATE_CSI;
          ts->param_count = 0;
          for(int p = 0; p < 16; ++p) ts->params[p] = 0;
        } else ts->state = TERM_STATE_NORMAL;
      } else if(ts->state == TERM_STATE_CSI) {
        if(c >= '0' && c <= '9')
          ts->params[ts->param_count] = ts->params[ts->param_count] * 10 + (c - '0');
        else if(c == ';') {
          if(ts->param_count < 15) ++ts->param_count;
        } else if(c == '?') {
          // advanced mode set prefix or something
          // later
        } else {
          int p1 = ts->params[0] == 0 ? 1 : ts->params[0];
          int p2 = ts->params[1] == 0 ? 1 : ts->params[1];
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
            case 'H':
            case 'f':
              ts->cy = p1 - 1;
              ts->cx = p2 - 1;
              if(ts->cy < 0) ts->cy = 0;
              if(ts->cy >= window->h) ts->cy = window->h - 1;
              if(ts->cx < 0) ts->cx = 0;
              if(ts->cx >= window->w) ts->cx = window->w - 1;
              break;
            case 'J': {
              int clear_type = ts->params[0];
              if(clear_type == 0) {
                for(int j = ts->cy; j < window->h; ++j) {
                  int start = (j == ts->cy) ? ts->cx : 0;
                  for(int i = start; i < window->w; ++i)
                    window->content[j * window->w + i] = (ts->current_attr << 8) | ' ';
                }
              } else if(clear_type == 1) {
                for(int j = 0; j <= ts->cy; ++j) {
                  int end = (j == ts->cy) ? ts->cx : window->w - 1;
                  for(int i = 0; i <= end; ++i)
                    window->content[j * window->w + i] = (ts->current_attr << 8) | ' ';
                }
              } else if(clear_type == 2) {
                for(int i = 0; i < window->w * window->h; ++i)
                  window->content[i] = (ts->current_attr << 8) | ' ';
              }
            } break;
            case 'K': {
              int clear_type = ts->params[0];
              if(clear_type == 0)
                for(int i = ts->cx; i < window->w; ++i)
                  window->content[ts->cy * window->w + i] = (ts->current_attr << 8) | ' ';
              else if(clear_type == 1)
                for(int i = 0; i <= ts->cx; ++i)
                  window->content[ts->cy * window->w + i] = (ts->current_attr << 8) | ' ';
              else if(clear_type == 2)
                for(int i = 0; i < window->w; ++i)
                  window->content[ts->cy * window->w + i] = (ts->current_attr << 8) | ' ';
            } break;
            case 'm': {
              for(int i = 0; i <= ts->param_count; ++i) {
                int p = ts->params[i];
                if(p == 0) ts->current_attr = 0x07;
                else if(p >= 30 && p <= 37) ts->current_attr = (ts->current_attr & 0xF0) | (p - 30);
                else if(p >= 40 && p <= 47)
                  ts->current_attr = (ts->current_attr & 0x0F) | ((p - 40) << 4);
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
          ts->state = TERM_STATE_NORMAL;
        }
      }
    }
  }
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
  ts->current_attr = 0b00000111;
  win->onevent = onevent;
  win->update = update;
  int flags = fcntl(ts->master_fd, F_GETFL, 0);
  fcntl(ts->master_fd, F_SETFL, flags | O_NONBLOCK);
}
