#include <linux/fb.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

sig_atomic_t keep_running = 1;

void handle_sigint(int signal_type)
{
  keep_running = 0;
}

int main() 
{
  signal(SIGINT, handle_sigint);

  int fd_fb0 = open("/dev/fb0", O_RDWR);
  if (fd_fb0 == -1) 
    { perror("/dev/fb0"); 
      goto cannot_open_fb0; }

  int fd_urandom = open("/dev/urandom", O_RDONLY);
  if (fd_urandom == -1) 
    { perror("/dev/urandom"); goto cannot_open_urandom; }
  
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  ioctl(fd_fb0, FBIOGET_VSCREENINFO, &vinfo);
  ioctl(fd_fb0, FBIOGET_FSCREENINFO, &finfo);

  uint32_t screen_x = vinfo.xres;
  uint32_t screen_y = vinfo.yres;

  vinfo.xoffset = 0;
  vinfo.yoffset = 0;

  printf("size %d, %d\n", screen_x, screen_y);
  printf("fds  %d, %d\n", fd_fb0, fd_urandom);
  fflush(stdout);

  void *memory = malloc(screen_x * screen_y * 4);

  while (keep_running) 
  {
    read(fd_urandom, memory, screen_x * screen_y * 4);
    write(fd_fb0,    memory, screen_x * screen_y * 4);
    lseek(fd_fb0, 0, SEEK_SET);
    ioctl(fd_fb0, FBIOPAN_DISPLAY, &vinfo);
  }
  
  close(fd_urandom);
  close(fd_fb0);
  return 0;

cannot_open_urandom:
  close(fd_fb0);
cannot_open_fb0:
  return 1;
}
