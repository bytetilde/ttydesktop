#include "tw.h"
#include <unistd.h>

void fill(int x, int y, int w, int h, char attr) {
  for(int i = y; i < y + h; i++)
    for(int j = x; j < x + w; j++) tw_putc(' ', j, i, attr);
}

int main() {
  tw_init();
  while(1) {
    int c = tw_getch();
    if(c != -1) {
      if(c == 'q') break;
    }
    tw_wh_t size = tw_get_size();
    tw_clear(0b01000000);
    fill(0, size.h - 1, size.w, 1, 0b01110000);
    tw_flush();
  }
  tw_deinit();
  return 0;
}