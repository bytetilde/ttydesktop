#pragma once
#include <stdbool.h>

typedef struct desktop_t desktop_t;
typedef struct window_t window_t;
typedef struct window_t {
  int x, y, w, h;
  char* title;
  short* content;
  void (*update)(window_t* window, desktop_t* desktop);
  void (*draw)(window_t* window, desktop_t* desktop);
  void (*onevent)(window_t* window, desktop_t* desktop, int event, void* data);
} window_t;
typedef struct desktop_t {
  window_t* windows;
  int window_count;
  int window_capacity;
  char* statusbar_text;
  bool (*before_update)(desktop_t* desktop);
  bool (*update)(desktop_t* desktop);
  bool (*after_update)(desktop_t* desktop);
  void (*before_draw)(desktop_t* desktop);
  void (*draw)(desktop_t* desktop);
  void (*after_draw)(desktop_t* desktop);
} desktop_t;