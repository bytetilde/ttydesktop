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

#include "../include/tw.h"
#include <ctype.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static struct termios orig_termios;
static pthread_mutex_t tw_mutex = PTHREAD_MUTEX_INITIALIZER;
int tw_w = 0, tw_h = 0;
uint16_t* tw_buf = NULL;
uint16_t* tw_back_buf = NULL;
static bool tw_keys[2048] = {0};
static int tw_peek_buf = -1;
static volatile sig_atomic_t tw_resized = 0;
static void tw_sigwinch_handler(int sig) {
  (void)sig;
  tw_resized = 1;
}
static void tw_check_resize() {
  if(!tw_resized) return;
  tw_resized = 0;
  tw_wh_t sz = tw_get_size();
  if(sz.w == tw_w && sz.h == tw_h) return;
  uint16_t* new_buf = malloc(sz.w * sz.h * sizeof(uint16_t));
  uint16_t* new_back_buf = malloc(sz.w * sz.h * sizeof(uint16_t));
  if(new_buf && new_back_buf) {
    free(tw_buf);
    free(tw_back_buf);
    tw_buf = new_buf;
    tw_back_buf = new_back_buf;
    tw_w = sz.w;
    tw_h = sz.h;
    memset(tw_buf, 0, tw_w * tw_h * sizeof(uint16_t));
    memset(tw_back_buf, 0, tw_w * tw_h * sizeof(uint16_t));
  } else {
    if(new_buf) free(new_buf);
    if(new_back_buf) free(new_back_buf);
  }
}
tw_wh_t tw_get_size() {
  struct winsize ws;
  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) return (tw_wh_t){80, 24};
  return (tw_wh_t){ws.ws_col, ws.ws_row};
}
void tw_init() {
  if(tw_buf) return;
  tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  printf("\033[?1049h");
  printf("\033[?25l");
  fflush(stdout);
  tw_wh_t sz = tw_get_size();
  tw_w = sz.w;
  tw_h = sz.h;
  tw_buf = malloc(tw_w * tw_h * sizeof(uint16_t));
  tw_back_buf = malloc(tw_w * tw_h * sizeof(uint16_t));
  if(!tw_buf || !tw_back_buf) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    if(tw_buf) free(tw_buf);
    if(tw_back_buf) free(tw_back_buf);
    tw_buf = NULL;
    tw_back_buf = NULL;
    fprintf(stderr, "tw: out of memory\n");
    exit(1);
  }
  memset(tw_buf, 0, tw_w * tw_h * sizeof(uint16_t));
  memset(tw_back_buf, 0, tw_w * tw_h * sizeof(uint16_t));
  signal(SIGWINCH, tw_sigwinch_handler);
  atexit(tw_deinit);
}
void tw_deinit() {
  if(!tw_buf) return;
  printf("\033[?25h");
  printf("\033[?1049l");
  fflush(stdout);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  free(tw_buf);
  free(tw_back_buf);
  tw_buf = NULL;
  tw_back_buf = NULL;
}
static inline void _tw_putc(char c, int x, int y, char attr) {
  if(x < 0 || x >= tw_w || y < 0 || y >= tw_h) return;
  tw_buf[y * tw_w + x] = ((uint16_t)(uint8_t)attr << 8) | (uint8_t)c;
}
void tw_putc(char c, int x, int y, char attr) {
  pthread_mutex_lock(&tw_mutex);
  _tw_putc(c, x, y, attr);
  pthread_mutex_unlock(&tw_mutex);
}
void tw_puts(const char* s, int x, int y, char attr) {
  pthread_mutex_lock(&tw_mutex);
  while(*s) _tw_putc(*s++, x++, y, attr);
  pthread_mutex_unlock(&tw_mutex);
}
void tw_fill(int x, int y, int w, int h, char attr) {
  pthread_mutex_lock(&tw_mutex);
  for(int i = y; i < y + h; ++i)
    for(int j = x; j < x + w; ++j) _tw_putc(' ', j, i, attr);
  pthread_mutex_unlock(&tw_mutex);
}
void tw_printf(int x, int y, char attr, const char* fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  tw_puts(buf, x, y, attr);
}
void tw_clear(char attr) {
  pthread_mutex_lock(&tw_mutex);
  uint16_t val = ((uint16_t)(uint8_t)attr << 8) | (uint8_t)' ';
  for(int i = 0; i < tw_w * tw_h; ++i) tw_buf[i] = val;
  pthread_mutex_unlock(&tw_mutex);
}
static void tw_write_all(const char* buf, int len) {
  while(len > 0) {
    ssize_t n = write(STDOUT_FILENO, buf, len);
    if(n <= 0) break;
    buf += n;
    len -= n;
  }
}
void tw_flush_region(int x, int y, int w, int h) {
  pthread_mutex_lock(&tw_mutex);
  tw_check_resize();
  if(x < 0) {
    w += x;
    x = 0;
  }
  if(y < 0) {
    h += y;
    y = 0;
  }
  if(x + w > tw_w) w = tw_w - x;
  if(y + h > tw_h) h = tw_h - y;
  if(w <= 0 || h <= 0) return;
  static char out[65536];
  int ptr = 0;
  char current_attr = -1;
  const int threshold = 64000;
  for(int j = y; j < y + h; ++j) {
    bool moved = false;
    for(int i = x; i < x + w; ++i) {
      uint16_t val = tw_buf[j * tw_w + i];
      if(val == tw_back_buf[j * tw_w + i]) {
        moved = false;
        continue;
      }
      tw_back_buf[j * tw_w + i] = val;
      if(!moved) {
        int n = snprintf(out + ptr, sizeof(out) - ptr, "\033[%d;%dH", j + 1, i + 1);
        if(n > 0) ptr += (n >= (int)(sizeof(out) - ptr)) ? (sizeof(out) - ptr - 1) : n;
        moved = true;
      }
      char attr = (val >> 8) & 0xFF;
      char c = val & 0xFF;
      if(attr != current_attr) {
        int fg = attr & 0x07;
        int fg_bright = (attr >> 3) & 1;
        int bg = (attr >> 4) & 0x07;
        int bg_bright = (attr >> 7) & 1;
        int n = snprintf(out + ptr, sizeof(out) - ptr, "\033[0;%d;%dm", (fg_bright ? 90 : 30) + fg,
                         (bg_bright ? 100 : 40) + bg);
        if(n > 0) ptr += (n >= (int)(sizeof(out) - ptr)) ? (sizeof(out) - ptr - 1) : n;
        current_attr = attr;
      }
      out[ptr++] = c ? c : ' ';
      if(ptr > threshold) {
        tw_write_all(out, ptr);
        ptr = 0;
        current_attr = -1;
        moved = false;
      }
    }
  }
  int n = snprintf(out + ptr, sizeof(out) - ptr, "\033[0m");
  if(n > 0) ptr += (n >= (int)(sizeof(out) - ptr)) ? (sizeof(out) - ptr - 1) : n;
  if(ptr > 0) tw_write_all(out, ptr);
  pthread_mutex_unlock(&tw_mutex);
}
void tw_flush() {
  tw_flush_region(0, 0, tw_w, tw_h);
}
bool tw_key_pressed() {
  if(tw_peek_buf != -1) return true;
  struct pollfd fds = {STDIN_FILENO, POLLIN, 0};
  return poll(&fds, 1, 0) > 0;
}
static int tw_read_raw() {
  unsigned char c;
  if(read(STDIN_FILENO, &c, 1) == 1) return c;
  return -1;
}
static int tw_read_timeout(int timeout_ms) {
  struct pollfd fds = {STDIN_FILENO, POLLIN, 0};
  if(poll(&fds, 1, timeout_ms) <= 0) return -1;
  return tw_read_raw();
}
static int tw_decode_key() {
  int c = tw_read_raw();
  if(c == -1) return -1;
  if(c == 27) { // ESC
    int next = tw_read_timeout(50);
    if(next == -1) return TW_KEY_ESC;
    if(next == '[') {
      int params[4] = {0, 0, 0, 0};
      int p_idx = 0;
      int code = tw_read_timeout(10);
      if(code == '[') {
        int letter = tw_read_timeout(10);
        if(letter >= 'A' && letter <= 'E') return TW_KEY_F1 + (letter - 'A');
        return TW_KEY_ESC;
      }
      while(code != -1 && (isdigit(code) || code == ';')) {
        if(code == ';') {
          if(p_idx < 3) ++p_idx;
        } else params[p_idx] = params[p_idx] * 10 + (code - '0');
        code = tw_read_timeout(10);
      }
      int modifiers = 0;
      int m_val = (p_idx > 0) ? params[1] : 0;
      if(m_val == 2) modifiers |= TW_MOD_SHIFT;
      if(m_val == 3) modifiers |= TW_MOD_ALT;
      if(m_val == 5) modifiers |= TW_MOD_CTRL;
      switch(code) {
        case 'A': return TW_KEY_UP | modifiers;
        case 'B': return TW_KEY_DOWN | modifiers;
        case 'C': return TW_KEY_RIGHT | modifiers;
        case 'D': return TW_KEY_LEFT | modifiers;
        case 'H': return TW_KEY_HOME | modifiers;
        case 'F': return TW_KEY_END | modifiers;
        case 'Z': return TW_KEY_TAB | TW_MOD_SHIFT;
        case '~':
          switch(params[0]) {
            case 1:
            case 7: return TW_KEY_HOME | modifiers;
            case 4:
            case 8: return TW_KEY_END | modifiers;
            case 2: return TW_KEY_INSERT | modifiers;
            case 3: return TW_KEY_DELETE | modifiers;
            case 5: return TW_KEY_PAGE_UP | modifiers;
            case 6: return TW_KEY_PAGE_DOWN | modifiers;
            default:
              if(params[0] >= 11 && params[0] <= 15)
                return (TW_KEY_F1 + (params[0] - 11)) | modifiers;
              if(params[0] >= 17 && params[0] <= 21)
                return (TW_KEY_F6 + (params[0] - 17)) | modifiers;
              if(params[0] >= 23 && params[0] <= 24)
                return (TW_KEY_F11 + (params[0] - 23)) | modifiers;
          }
          break;
      }
    } else if(next == 'O') {
      int code = tw_read_timeout(10);
      switch(code) {
        case 'A': return TW_KEY_UP;
        case 'B': return TW_KEY_DOWN;
        case 'C': return TW_KEY_RIGHT;
        case 'D': return TW_KEY_LEFT;
        case 'H': return TW_KEY_HOME;
        case 'F': return TW_KEY_END;
        case 'P': return TW_KEY_F1;
        case 'Q': return TW_KEY_F2;
        case 'R': return TW_KEY_F3;
        case 'S': return TW_KEY_F4;
        default: return TW_KEY_ESC;
      }
    } else return next | TW_MOD_ALT;
    return TW_KEY_ESC;
  }
  if(c == 13 || c == 10) return TW_KEY_ENTER;
  if(c == 127 || c == 8) return TW_KEY_BACKSPACE;
  if(c == 9) return TW_KEY_TAB;
  if(c >= 0xC0) {
    int len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
    int full_char = c;
    for(int i = 1; i < len; ++i) {
      int next = tw_read_timeout(10);
      if(next != -1) full_char = (full_char << 8) | next;
    }
    return full_char;
  }
  if(c > 0 && c < 27 && c != 9 && c != 10 && c != 13 && c != 27) return (c + 'a' - 1) | TW_MOD_CTRL;
  return c;
}
int tw_getch() {
  int key;
  if(tw_peek_buf != -1) {
    key = tw_peek_buf;
    tw_peek_buf = -1;
  } else {
    if(!tw_key_pressed()) return -1;
    key = tw_decode_key();
  }
  if(key != -1) {
    memset(tw_keys, 0, sizeof(tw_keys));
    int base_key = key & 0xFFFF;
    if(base_key >= 0 && base_key < 2048) tw_keys[base_key] = true;
  }
  return key;
}
int tw_waitch() {
  while(!tw_key_pressed()) usleep(10000);
  return tw_getch();
}
int tw_peekch() {
  if(tw_peek_buf == -1) {
    if(tw_key_pressed()) tw_peek_buf = tw_decode_key();
  }
  return tw_peek_buf;
}
bool tw_is_key_down(int key) {
  if(key < 0 || key >= 2048) return false;
  return tw_keys[key];
}
