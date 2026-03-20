#pragma once
#include <stdbool.h>

typedef struct desktop_t desktop_t;
typedef struct window_t window_t;
typedef enum window_event_t {
  WINDOW_EVENT_NONE,
  WINDOW_EVENT_KEY,
  WINDOW_EVENT_MOVE,
  WINDOW_EVENT_RESIZE,
  WINDOW_EVENT_FOCUS,
  WINDOW_EVENT_UNFOCUS,
  WINDOW_EVENT_CLOSE,
  WINDOW_EVENT_OPEN,
  WINDOW_EVENT_CLOSE_FORCE,
} window_event_t;
typedef struct window_move_event_t {
  int dx, dy;
} window_move_event_t;
typedef struct window_resize_event_t {
  int dw, dh;
} window_resize_event_t;
typedef int window_key_event_t;
typedef struct window_t {
  int x, y, w, h;
  char* title;
  short* content;
  bool hidden;
  bool unmovable;
  bool unresizable;
  void (*update)(window_t* window, desktop_t* desktop);
  void (*draw)(window_t* window, desktop_t* desktop);
  bool (*onevent)(window_t* window, desktop_t* desktop, int event, void* data);
  void* handle;
} window_t;
typedef enum desktop_state_t {
  STATE_NORMAL,
  STATE_PROMPT_FOCUS,
  STATE_PROMPT_MOVE,
  STATE_PROMPT_RESIZE,
  STATE_PROMPT_CLOSE,
  STATE_PROMPT_OPEN,
  STATE_FOCUSED,
  STATE_MOVING,
  STATE_RESIZING
} desktop_state_t;
typedef struct desktop_t {
  window_t* windows;
  int window_count;
  int window_capacity;
  char* statustext;
  desktop_state_t state;
  char buf[256];
  int buflen;
  int target;
  int ox, oy, ow, oh;
  bool (*before_update)(desktop_t* desktop);
  bool (*update)(desktop_t* desktop);
  bool (*after_update)(desktop_t* desktop);
  void (*before_draw)(desktop_t* desktop);
  void (*draw)(desktop_t* desktop);
  void (*before_window_draw)(desktop_t* desktop, window_t* window, int i);
  void (*after_window_draw)(desktop_t* desktop, window_t* window, int i);
  void (*after_draw)(desktop_t* desktop);
  void (*on_status_change)(desktop_t* desktop, const char* new_status);
  void (*before_draw_status_bar)(desktop_t* desktop);
  void (*draw_status_bar)(desktop_t* desktop);
  // void (*after_draw_status_bar)(desktop_t* desktop);
  bool (*before_key)(desktop_t* desktop, int key);
  bool (*on_key)(desktop_t* desktop, int key);
  void (*after_key)(desktop_t* desktop, int key);
  bool (*before_window_event)(desktop_t* desktop, window_t* window, int index, int event,
                              void* data);
  void (*after_window_event)(desktop_t* desktop, window_t* window, int index, int event,
                             void* data);
  void (*before_window_update)(desktop_t* desktop, window_t* window, int index);
  void (*after_window_update)(desktop_t* desktop, window_t* window, int index);
} desktop_t;
