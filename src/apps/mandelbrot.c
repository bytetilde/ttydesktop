#include "commonapi.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#define THREADS 4

typedef struct {
  double x_min, x_max, y_min, y_max;
  int max_iter;
  bool needs_redraw;
} mandelbrot_state_t;
typedef struct {
  int start_y, end_y;
  int width, height;
  window_t* window;
  mandelbrot_state_t* s;
} thread_arg_t;
int mandelbrot(double cr, double ci, int max_iter) {
  double zr = 0, zi = 0;
  int iter = 0;
  while(zr * zr + zi * zi <= 4.0 && iter < max_iter) {
    double temp = zr * zr - zi * zi + cr;
    zi = 2.0 * zr * zi + ci;
    zr = temp;
    iter++;
  }
  return iter;
}
void* render_chunk(void* arg) {
  thread_arg_t* t = (thread_arg_t*)arg;
  mandelbrot_state_t* s = t->s;
  for(int y = t->start_y; y < t->end_y; ++y) {
    for(int x = 0; x < t->width; ++x) {
      double cr = s->x_min + (double)x / t->width * (s->x_max - s->x_min);
      double ci = s->y_min + (double)y / t->height * (s->y_max - s->y_min);
      int iter = mandelbrot(cr, ci, s->max_iter);
      // float reliter = (float)iter / s->max_iter;
      // char c = (iter == s->max_iter) ? ' ' : ".:-=+*#%@"[(int)(reliter * 9)];
      char c = (iter == s->max_iter) ? ' ' : ".:-=+*#%@"[iter % 9];
      unsigned char color = (iter * 27 % 15) + 1;
      t->window->content[y * t->width + x] = (unsigned short)(c | (color << 8));
    }
  }
  return NULL;
}
void update(window_t* window, desktop_t* desktop) {
  (void)desktop;
  mandelbrot_state_t* s = window->data;
  if(!s || !s->needs_redraw) return;
  pthread_t threads[THREADS];
  thread_arg_t args[THREADS];
  int rows_per_thread = window->h / THREADS;
  for(int i = 0; i < THREADS; ++i) {
    args[i].start_y = i * rows_per_thread;
    args[i].end_y = (i == THREADS - 1) ? window->h : (i + 1) * rows_per_thread;
    args[i].width = window->w;
    args[i].height = window->h;
    args[i].window = window;
    args[i].s = s;
    pthread_create(&threads[i], NULL, render_chunk, &args[i]);
  }
  for(int i = 0; i < THREADS; ++i) pthread_join(threads[i], NULL);
  s->needs_redraw = false;
}
bool onevent(window_t* window, desktop_t* desktop, int event, void* data) {
  (void)desktop;
  mandelbrot_state_t* s = window->data;
  if(event == WINDOW_EVENT_CLOSE) {
    free(window->title);
    free(window->content);
    free(s);
    window->data = NULL;
  }
  if(event == WINDOW_EVENT_RESIZE) {
    short* tmp = realloc(window->content, window->w * window->h * sizeof(short));
    if(tmp) {
      window->content = tmp;
      s->needs_redraw = true;
    } else {
      window->close_pending = 1;
    }
  }
  if(event == WINDOW_EVENT_KEY) {
    long key = (long)data;
    double dx = (s->x_max - s->x_min) * 0.1;
    double dy = (s->y_max - s->y_min) * 0.1;
    if(key == 'w') {
      s->y_min -= dy;
      s->y_max -= dy;
    } else if(key == 's') {
      s->y_min += dy;
      s->y_max += dy;
    } else if(key == 'a') {
      s->x_min -= dx;
      s->x_max -= dx;
    } else if(key == 'd') {
      s->x_min += dx;
      s->x_max += dx;
    } else if(key == '+') {
      s->x_min += dx;
      s->x_max -= dx;
      s->y_min += dy;
      s->y_max -= dy;
    } else if(key == '-') {
      s->x_min -= dx;
      s->x_max += dx;
      s->y_min -= dy;
      s->y_max += dy;
    } else if(key == 'i') {
      s->max_iter += 20;
    } else if(key == 'u') {
      if(s->max_iter > 20) s->max_iter -= 20;
    }
    s->needs_redraw = true;
  }
  return false;
}

void window_init(desktop_t* desktop, window_t* win) {
  (void)desktop;
  mandelbrot_state_t* s = calloc(1, sizeof(mandelbrot_state_t));
  s->x_min = -2.0;
  s->x_max = 1.0;
  s->y_min = -1.2;
  s->y_max = 1.2;
  s->max_iter = 50;
  s->needs_redraw = true;
  win->title = strdup("mandelbrot");
  win->x = 0;
  win->y = 0;
  win->w = 30;
  win->h = 10;
  win->content = calloc(win->w * win->h, sizeof(short));
  win->data = s;
  win->update = update;
  win->onevent = onevent;
}
