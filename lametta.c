
#include <pthread.h>
#include <curses.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <string.h>
#include <errno.h>

// helper macros
#define coladdstr(C, S) do { \
    if (lights) attron(COLOR_PAIR(C)); \
    addstr(S); \
    attroff(COLOR_PAIR(C)); } while (0)
#define colprintw(C, F, ...) do { \
    if (lights) attron(COLOR_PAIR(C)); \
    printw(F, __VA_ARGS__); \
    attroff(COLOR_PAIR(C)); } while (0)

// global state
static volatile int lights = 0;
static int rainbow = 0;

static int nthreads = 0;
static pthread_t threads[100];
static int thread_args[100] = { 0 };
static int thread_ids[100] = { 0 };
static volatile int tlights[100] = { 0 };
static int prio_state[7] = { 0 };
static char error_str[256] = { 0 };

static pid_t
gettid (void)
{
  return syscall(SYS_gettid);
}

static int
get_year (void)
{
  time_t t = time(NULL);
  struct tm *now = localtime(&t);
  return now->tm_year + 1901;
}

static void
set_error (const char *str)
{
  snprintf(error_str, 256, "error: %s: %s", str, strerror(errno));
  error_str[255] = '\0';
}

static void
counter_advance_every_nsec(int *c, int nsec)
{
  static struct timespec last = { 0 };
  if (!last.tv_sec)
    clock_gettime(CLOCK_REALTIME, &last);

  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);

  if ((now.tv_sec - last.tv_sec) * 1000000000 + (now.tv_nsec - last.tv_nsec) > nsec)
    {
      last = now;
      (*c)++;
    }
}

static void
toggle_lights(void)
{
  lights = !lights;
  if (lights)
    return;

  int i;
  for (i = 0; i < 100; ++i)
    {
      if (!thread_args[i])
        continue;

      thread_args[i] = 0;
      int res = pthread_join(threads[i], NULL);
      if (res)
        set_error("pthread_join");
      nthreads--;
    }

  for (i = 0; i < 7; ++i)
    prio_state[i] = 0;
}

static void
set_prio (int num)
{
  if (!lights)
    return;

  prio_state[num] = !prio_state[num];

  int i;
  for (i = 0; i < 100; ++i)
    if (thread_args[i] && i % 7 == num)
      {
        int res = setpriority(PRIO_PROCESS, thread_ids[i], prio_state[num] ? 1 : 20);
        if (res)
          set_error("setpriority");
      }
}

static void
clear_prio (void)
{
  int i;
  for (i = 0; i < 100; ++i)
    if (thread_args[i])
      {
        int res = setpriority(PRIO_PROCESS, thread_ids[i], 20);
        if (res)
          set_error("setpriority");
      }

  for (i = 0; i < 7; ++i)
    prio_state[i] = 0;
}

static void*
thread_func (void *arg)
{
  int pos = *(int*)arg;

  thread_ids[pos] = gettid();
  int res = setpriority(PRIO_PROCESS, thread_ids[pos], prio_state[pos % 7] ? 1 : 20);
  if (res)
    set_error("thread_func:setpriority");

  while (lights)
    tlights[pos] = 1;

  return NULL;
}

static void
add_thread(void)
{
  if ((!lights) || (nthreads >= 99))
    return;

  int pos;
  while(thread_args[pos = rand() % 100]);

  thread_args[pos] = pos;
  int res = pthread_create(threads + pos, NULL, &thread_func, thread_args + pos);
  if (res)
    {
      set_error("pthread_create");
      thread_args[pos] = 0;
      return;
    }

  ++nthreads;
}

static void
draw_tree(void)
{
  int center = (getmaxx(stdscr) - 1) / 2;
  int x = center;
  int y = 1;
  int i, j, k;

  // clear everything
  erase();

  // draw title
  move(0, 1);
  addstr("lametta");

  // draw leaves and baubles
  for (i = 0, k = 0; i < 10; ++i)
    {
      move(y++, x--);
      for (j = 0; j <= 2*i; ++j, ++k)
        {
          if (thread_args[k])
            {
              attron(A_BOLD);
              coladdstr(tlights[k] ? k % 7 : 7, "o");
              attroff(A_BOLD);
              tlights[k] = 0;
            }
          else
            coladdstr(4, "*");
        }
    }

  // draw trunk
  move(y++, center - 1);
  coladdstr(3, "mWm");
  move(y++, center - 2);
  coladdstr(3, "/MWM\\");

  // draw message
  y++;
  move(y++, center - 7);
  attron(A_BOLD);
  coladdstr(2, "MERRY CHRISTMAS");
  move(y++, center - 11);
  coladdstr(2, "And lots of ");
  counter_advance_every_nsec(&rainbow, 250000000);
  rainbow %= 6;
  coladdstr(rainbow + 1, "C");
  coladdstr((rainbow + 1) % 6 + 1, "O");
  coladdstr((rainbow + 2) % 6 + 1, "D");
  coladdstr((rainbow + 3) % 6 + 1, "E");
  colprintw(2, " in %i", get_year());
  attroff(A_BOLD);

  // draw legend
  y++;
  move(y++, 4);
  addstr("q: quit");
  move(y++, 4);
  addstr("l: toggle lights on/off");
  if (lights)
    {
      move(y++, 4);
      addstr("t: add bauble (requires lights)");
      move(y++, 4);
      addstr("m: reset bauble priorities");
      y++;
      move(y++, 4);
      addstr("baubles: ");
      for (i = 0; i < 7; ++i)
        {
          colprintw(i + 8 * prio_state[i], "%i", i);
          addstr(" ");
        }
    }

  // draw errors (if any)
  move(getmaxy(stdscr)-1, 0);
  attron(COLOR_PAIR(8));
  addstr(error_str);
  attroff(COLOR_PAIR(8));

  // draw and return to main loop
  refresh();
}

int
main (void)
{
  // prpare curses window
  initscr();
  curs_set(0);
  nodelay(stdscr, 1);
  noecho();
  refresh();
  srand(time(NULL));

  // initialize used colors
  start_color();
  init_pair(1, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(2, COLOR_RED, COLOR_BLACK);
  init_pair(3, COLOR_YELLOW, COLOR_BLACK);
  init_pair(4, COLOR_GREEN, COLOR_BLACK);
  init_pair(5, COLOR_CYAN, COLOR_BLACK);
  init_pair(6, COLOR_BLUE, COLOR_BLACK);
  init_pair(7, COLOR_BLACK, COLOR_BLACK);
  init_pair(8+0, COLOR_BLACK, COLOR_WHITE);
  init_pair(8+1, COLOR_BLACK, COLOR_MAGENTA);
  init_pair(8+2, COLOR_BLACK, COLOR_RED);
  init_pair(8+3, COLOR_BLACK, COLOR_YELLOW);
  init_pair(8+4, COLOR_BLACK, COLOR_GREEN);
  init_pair(8+5, COLOR_BLACK, COLOR_CYAN);
  init_pair(8+6, COLOR_BLACK, COLOR_BLUE);

  // begin main loop
  while(1)
    {
      char c = getch();
      if (c == 'q')
        break;
      switch (c)
        {
          case 'l':
            toggle_lights();
            break;
          case 'c':
            error_str[0] = '\0';
            break;
          case 't':
            add_thread();
            break;
          case '0': case '1': case '2': case '3':
          case '4': case '5': case '6':
            set_prio(c - '0');
            break;
          case 'm':
            clear_prio();
            break;
        }

      draw_tree();
    }

  // deinitialize curses window
  endwin();

  return 0;
}
