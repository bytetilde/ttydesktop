#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct tw_wh_t {
  int w, h;
} tw_wh_t;
void tw_init();
void tw_deinit();
tw_wh_t tw_get_size();
void tw_putc(char c, int x, int y, char attr);
void tw_puts(const char* s, int x, int y, char attr);
void tw_fill(int x, int y, int w, int h, char attr);
void tw_printf(int x, int y, char attr, const char* fmt, ...);
void tw_clear(char attr);
void tw_flush();
void tw_flush_region(int x, int y, int w, int h);
typedef enum tw_key_t {
  TW_KEY_UP = 1000,
  TW_KEY_DOWN,
  TW_KEY_LEFT,
  TW_KEY_RIGHT,
  TW_KEY_ESC,
  TW_KEY_ENTER,
  TW_KEY_BACKSPACE,
  TW_KEY_TAB,
} tw_key_t;
#define TW_MOD_SHIFT 0x10000
#define TW_MOD_CTRL 0x20000
#define TW_MOD_ALT 0x40000
int tw_getch();
int tw_waitch();
int tw_peekch();
bool tw_key_pressed();
bool tw_is_key_down(int key);
