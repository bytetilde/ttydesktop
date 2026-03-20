#include "commonapi.h"
#include "tw.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
void fill(int x, int y, int w, int h, char attr) {
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

bool desktop_update(desktop_t* desktop) {
  if(desktop->before_update && desktop->before_update(desktop)) return true;
  // TODO: key down key up etc
  if(tw_getch() == 'q') return true;
  for(int i = 0; i < desktop->window_count; ++i)
    desktop->windows[i].update(desktop->windows + i, desktop);
  if(desktop->after_update && desktop->after_update(desktop)) return true;
  return false;
}
void desktop_draw(desktop_t* desktop) {
  if(desktop->before_draw) desktop->before_draw(desktop);
  tw_wh_t size = tw_get_size();
  tw_clear(0b01000000);
  fill(0, size.h - 1, size.w, 1, 0b01110000);
  tw_puts(desktop->statusbar_text, 0, size.h - 1, 0b01110000);
  for(int i = 0; i < desktop->window_count; ++i) {
    desktop->windows[i].draw(desktop->windows + i, desktop);
    fill(desktop->windows[i].x, desktop->windows[i].y, desktop->windows[i].w, 1, 0b00110000);
    tw_printf(desktop->windows[i].x + 1, desktop->windows[i].y, 0b00110000, "[%d] %s", i,
              desktop->windows[i].title);
    for(int j = 0; j < desktop->windows[i].y; ++j) {
      for(int k = 0; k < desktop->windows[i].x; ++k) {
        short c = desktop->windows[i].content[j * desktop->windows[i].w + k];
        tw_putc(c & 255, desktop->windows[i].x + k, desktop->windows[i].y + j, c >> 8);
      }
    }
  }
  if(desktop->after_draw) desktop->after_draw(desktop);
}

// TEMPORARY
static int bx = 0, by = 0, bvx = 1, bvy = 1;
void after_desktop_draw(desktop_t* desktop) {
  (void)desktop;
  tw_wh_t size = tw_get_size();
  bx += bvx;
  by += bvy;
  if(bx < 0) {
    bx = 0;
    bvx = 1;
  }
  if(by < 0) {
    by = 0;
    bvy = 1;
  }
  if(bx >= size.w) {
    bx = size.w - 1;
    bvx = -1;
  }
  if(by >= size.h) {
    by = size.h - 1;
    bvy = -1;
  }
  tw_putc('@', bx, by, 0b00001010);
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
    .after_draw = after_desktop_draw,
  };
  tw_wh_t size = tw_get_size();
  desktop.statusbar_text = calloc(size.w + 1, sizeof(char));
  snprintf(desktop.statusbar_text, size.w + 1, "statusbar text");
  while(1) {
    if(desktop.update(&desktop)) break;
    desktop.draw(&desktop);
    tw_flush();
    usleep(33333);
  }
  return 0;
}