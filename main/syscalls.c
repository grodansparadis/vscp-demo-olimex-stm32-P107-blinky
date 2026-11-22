/*
  syscalls.c - printf output to the RS232 port (USART2)
*/

#include <errno.h>
#include <sys/stat.h>

void board_log_putc(char c);

int
_write(int fd, const char *buf, int len)
{
  for (int i = 0; i < len; i++) {
    board_log_putc(buf[i]);
  }
  return len;
}

// Not used: stubs so newlib links without warnings
int
_close(int fd)
{
  return -1;
}

int
_fstat(int fd, struct stat *st)
{
  st->st_mode = S_IFCHR;
  return 0;
}

int
_isatty(int fd)
{
  return 1;
}

int
_lseek(int fd, int ptr, int dir)
{
  return 0;
}

int
_read(int fd, char *ptr, int len)
{
  return 0;
}

int
_getpid(void)
{
  return 1;
}

int
_kill(int pid, int sig)
{
  errno = EINVAL;
  return -1;
}
