#include "../include/tw.h"
#include <ctype.h>
#include <poll.h>
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
static int tw_w = 0, tw_h = 0;
static uint16_t* tw_buf = NULL;
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
  uint16_t* new_buf = realloc(tw_buf, sz.w * sz.h * sizeof(uint16_t));
  if(new_buf) {
    tw_buf = new_buf;
    tw_w = sz.w;
    tw_h = sz.h;
    memset(tw_buf, 0, tw_w * tw_h * sizeof(uint16_t));
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
  memset(tw_buf, 0, tw_w * tw_h * sizeof(uint16_t));
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
  tw_buf = NULL;
}
void tw_putc(char c, int x, int y, char attr) {
  if(x < 0 || x >= tw_w || y < 0 || y >= tw_h) return;
  tw_buf[y * tw_w + x] = ((uint16_t)(uint8_t)attr << 8) | (uint8_t)c;
}
void tw_puts(const char* s, int x, int y, char attr) {
  while(*s) tw_putc(*s++, x++, y, attr);
}
void tw_fill(int x, int y, int w, int h, char attr) {
  for(int i = y; i < y + h; i++)
    for(int j = x; j < x + w; j++) tw_putc(' ', j, i, attr);
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
  uint16_t val = ((uint16_t)(uint8_t)attr << 8) | (uint8_t)' ';
  for(int i = 0; i < tw_w * tw_h; i++) tw_buf[i] = val;
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
  for(int j = y; j < y + h; j++) {
    ptr += snprintf(out + ptr, sizeof(out) - ptr, "\033[%d;%dH", j + 1, x + 1);
    for(int i = x; i < x + w; i++) {
      uint16_t val = tw_buf[j * tw_w + i];
      char attr = (val >> 8) & 0xFF;
      char c = val & 0xFF;
      if(attr != current_attr) {
        int fg = attr & 0x07;
        int fg_bright = (attr >> 3) & 1;
        int bg = (attr >> 4) & 0x07;
        int bg_bright = (attr >> 7) & 1;
        ptr += snprintf(out + ptr, sizeof(out) - ptr, "\033[0;%d;%dm", (fg_bright ? 90 : 30) + fg,
                        (bg_bright ? 100 : 40) + bg);
        current_attr = attr;
      }
      out[ptr++] = c ? c : ' ';
      if(ptr > threshold) {
        tw_write_all(out, ptr);
        ptr = 0;
      }
    }
  }
  ptr += snprintf(out + ptr, sizeof(out) - ptr, "\033[0m");
  if(ptr > 0) tw_write_all(out, ptr);
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
static int tw_decode_key() {
  int c = tw_read_raw();
  if(c == 27) { // ESC
    struct pollfd fds = {STDIN_FILENO, POLLIN, 0};
    if(poll(&fds, 1, 50) <= 0) return TW_KEY_ESC;
    int next = tw_read_raw();
    if(next == '[') {
      int val = 0;
      int code = tw_read_raw();
      while(isdigit(code)) {
        val = val * 10 + (code - '0');
        code = tw_read_raw();
      }
      if(code == 'A') return TW_KEY_UP;
      if(code == 'B') return TW_KEY_DOWN;
      if(code == 'C') return TW_KEY_RIGHT;
      if(code == 'D') return TW_KEY_LEFT;
      if(code == 'Z') return TW_KEY_TAB | TW_MOD_SHIFT;
      if(code == '~') {
        // TODO buh
      }
      if(code == ';') {
        int mod = tw_read_raw();
        int real_code = tw_read_raw();
        int modifiers = 0;
        if(mod == '2') modifiers |= TW_MOD_SHIFT;
        if(mod == '3') modifiers |= TW_MOD_ALT;
        if(mod == '5') modifiers |= TW_MOD_CTRL;
        if(real_code == 'A') return TW_KEY_UP | modifiers;
        if(real_code == 'B') return TW_KEY_DOWN | modifiers;
        if(real_code == 'C') return TW_KEY_RIGHT | modifiers;
        if(real_code == 'D') return TW_KEY_LEFT | modifiers;
      }
    } else {
      return next | TW_MOD_ALT;
    }
  }
  if(c == 13) return TW_KEY_ENTER;
  if(c == 127) return TW_KEY_BACKSPACE;
  if(c == 9) return TW_KEY_TAB;
  if(c > 0 && c < 27) return (c + 'a' - 1) | TW_MOD_CTRL;
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
    if(key < 2048) tw_keys[key] = true;
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
