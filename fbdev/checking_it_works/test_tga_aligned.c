#include <linux/fb.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>

#define TGA_ID_FROM_TOP_LEFT  0x20
#define TGA_ID_ALPHA_CHANNEL  0x08
#define TGA_32_BITS_PER_PIXEL 0x20
#define TGA_IT_UNCOMPRESSED_TRUE_COLOR 0x02

struct tga_texture {
  uint8_t  image_id;
  uint8_t  color_map_type;
  uint8_t  image_type;
  uint8_t  color_map_spec[5];
  uint16_t x_origin;
  uint16_t y_origin;
  uint16_t width;
  uint16_t height;
  uint8_t  pixel_depth;
  uint8_t  image_description;
  uint8_t  _[46];
  uint8_t  pixels[];
} __attribute__((packed));

void alpha_channel_ff(uint32_t *pixels, uint32_t pixels_count) 
{
  for (uint32_t i = 0; i < pixels_count; ++i) 
  {
    pixels[i] |= 0xFF << 24;
  }
}

int main() 
{
  int fd_fb0  = open("/dev/fb0", O_RDWR);
  int fd_blit = open("blit.tga", O_RDWR | O_CREAT);

  fchmod(fd_blit, 0777);

  if (fd_fb0 == -1) 
  {
    perror("/dev/fb0"); 
    goto cannot_open_fb0; 
  }

  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;

  ioctl(fd_fb0, FBIOGET_VSCREENINFO, &vinfo);
  ioctl(fd_fb0, FBIOGET_FSCREENINFO, &finfo);

  uint16_t screen_x = vinfo.xres;
  uint16_t screen_y = vinfo.yres;

  uint32_t bytes_x  = finfo.line_length;
  uint32_t bytes_xy = screen_y * bytes_x;

  
  // read the screen 
  // we expect it to be 0xARGB or flipped BGRA in memory
  uint32_t texture_size = sizeof(struct tga_texture) + bytes_xy;
  struct tga_texture *texture = memset(malloc(texture_size), 0, texture_size);

  texture->width  = screen_x;
  texture->height = screen_y;
  texture->pixel_depth = TGA_32_BITS_PER_PIXEL;
  texture->image_id = sizeof(texture->_);

  texture->image_type        |= TGA_IT_UNCOMPRESSED_TRUE_COLOR;
  // texture->image_description |= TGA_ID_FROM_TOP_LEFT;
  texture->image_description |= TGA_ID_ALPHA_CHANNEL;

  read(fd_fb0, texture->pixels, bytes_xy);
  alpha_channel_ff((void*)texture->pixels, bytes_xy >> 2);
  uint32_t written = write(fd_blit, texture, texture_size);
  printf("size %d, %d\n", screen_x, screen_y);
  printf("written: %d bytes\n", written);
  
  return 0;

cannot_open_fb0:
  return 1;
}
