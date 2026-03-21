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

#pragma once
#include <stdbool.h>
#include <stdint.h>

extern int tw_w, tw_h;
extern uint16_t* tw_buf;

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
  TW_KEY_HOME,
  TW_KEY_END,
  TW_KEY_PAGE_UP,
  TW_KEY_PAGE_DOWN,
  TW_KEY_INSERT,
  TW_KEY_DELETE,
  TW_KEY_F1,
  TW_KEY_F2,
  TW_KEY_F3,
  TW_KEY_F4,
  TW_KEY_F5,
  TW_KEY_F6,
  TW_KEY_F7,
  TW_KEY_F8,
  TW_KEY_F9,
  TW_KEY_F10,
  TW_KEY_F11,
  TW_KEY_F12,
} tw_key_t;
#define TW_MOD_SHIFT 0x10000
#define TW_MOD_CTRL 0x20000
#define TW_MOD_ALT 0x40000
int tw_getch();
int tw_waitch();
int tw_peekch();
bool tw_key_pressed();
bool tw_is_key_down(int key);
