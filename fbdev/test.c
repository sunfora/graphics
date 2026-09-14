#include <cairo.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/kd.h>
#include <sys/vt.h>
#include <signal.h>
#include <time.h>
#include <math.h>

#include <linux/sched.h>    /* Definition of struct clone_args */
#include <sched.h>          /* Definition of CLONE_* constants */
#include <sys/syscall.h>    /* Definition of SYS_* constants */
#include <unistd.h>

#include <sys/wait.h>

#include <sys/stat.h>


int32_t fd_tty;

volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t now_afk = 0;
volatile sig_atomic_t was_afk = 0;

void handle_sigint(int sig) {
    keep_running = 0; 
}

double get_time_sec(void) {
    struct timespec ts;
    // syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}


#define interpret(type_name, ptr, ...) \
    struct __attribute__((packed, may_alias)) type_name { __VA_ARGS__ } *type_name = \
    (struct type_name *)(ptr)

struct config {
  int32_t x;
  int32_t y;
  uint32_t rgba_background;
};

#define TGA_ALIGN_DWORD 2
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
  uint8_t  _[2];
  uint8_t  pixels[];
} __attribute__((packed));

void virtual_terminal_switch(int signal) {
  if (signal == SIGUSR1) {
    now_afk = 1; 
  }
  if (signal == SIGUSR2) {
    now_afk = 0;
  }
}

void alpha_blend_memcpy(void* restrict bytes_d,
                        void* restrict bytes_a, 
                        void* restrict bytes_b, 
                        uint32_t bytes)
{
  uint32_t times = bytes / 4;
  uint32_t* color_a = bytes_a;
  uint32_t* color_b = bytes_b;
  uint32_t* color_d = bytes_d;

  for (uint32_t i = 0; i < times; ++i) {
    uint32_t a = color_a[i];
    uint32_t b = color_b[i];
    uint32_t d = 0;

    uint8_t a_a = a >> 24;
    uint8_t a_r = a >> 16;
    uint8_t a_g = a >> 8;
    uint8_t a_b = a >> 0;

    uint8_t b_a = b >> 24;
    uint8_t b_r = b >> 16;
    uint8_t b_g = b >> 8;
    uint8_t b_b = b >> 8;

    uint32_t alpha   = a_a;
    uint32_t i_alpha = 0xFF - alpha;
    
    // review this trick
    uint8_t d_b = ((a_b * alpha) + (b_b * i_alpha) + 128) >> 8;
    uint8_t d_g = ((a_g * alpha) + (b_g * i_alpha) + 128) >> 8;
    uint8_t d_r = ((a_r * alpha) + (b_r * i_alpha) + 128) >> 8;
    uint8_t d_a = 0xFF;

    d |= (d_a << 24) 
      |  (d_r << 16) 
      |  (d_g << 8)  
      |  (d_b << 0);

    color_d[i] = d;
  }
}

int real_main() {

    signal(SIGINT, handle_sigint);

    // open up live config
    int32_t pagesize = sysconf(_SC_PAGESIZE); // just make a page
    volatile struct config* cfg = NULL;
    {
      int32_t permissions = 0777; // ignore permissions issues for now

      int32_t config_fd = open("config.bin", O_RDWR | O_CREAT);
      fchmod(config_fd, permissions);
      ftruncate(config_fd, pagesize);

      cfg = mmap(
          NULL, pagesize,
          PROT_READ | PROT_WRITE,
          MAP_SHARED, config_fd, 0
      );
      close(config_fd);
    }


    fd_tty = open("/dev/tty0", O_RDWR);
    int32_t fd_fb0 = open("/dev/fb0",  O_RDWR);

    struct fb_var_screeninfo v_info;
    struct fb_fix_screeninfo f_info;

    ioctl(fd_fb0, FBIOGET_VSCREENINFO, &v_info);
    ioctl(fd_fb0, FBIOGET_FSCREENINFO, &f_info);


    // allow switching between virtual terminals
    signal(SIGUSR1, virtual_terminal_switch);
    signal(SIGUSR2, virtual_terminal_switch);
    struct vt_mode mode = {
        .mode   = VT_PROCESS,
        .relsig = SIGUSR1,
        .acqsig = SIGUSR2
    };
    ioctl(fd_tty, VT_SETMODE, &mode);


    if (ioctl(fd_tty, KDSETMODE, KD_GRAPHICS) != 0) {
      perror("KD_GRAPHICS failed\n");
    }
    

  
    // read screen sizes  
    int32_t screen_x = v_info.xres;
    int32_t screen_y = v_info.yres;

    int32_t virtual_screen_x = v_info.xres_virtual;
    int32_t virtual_screen_y = v_info.yres_virtual;

    int32_t fb0_size = f_info.smem_len;
    int32_t line_length = f_info.line_length;


    // open up live texture
    // note: pretty much copypasted from above
    volatile struct tga_texture* texture = NULL;
    int32_t texture_size = sizeof(struct tga_texture) + line_length * screen_y;
    {
      int32_t permissions = 0777; // ignore permissions issues for now

      int32_t texture_fd = open("texture.tga", O_RDWR | O_CREAT);
      fchmod(texture_fd, permissions);
      ftruncate(texture_fd, texture_size);

      texture = mmap(
          NULL, texture_size,
          PROT_READ | PROT_WRITE,
          MAP_SHARED, texture_fd, 0
      );
      
      texture->image_type        |= TGA_IT_UNCOMPRESSED_TRUE_COLOR;
      texture->image_description |= TGA_ID_ALPHA_CHANNEL;
      texture->image_description |= TGA_ID_FROM_TOP_LEFT;

      texture->image_id = TGA_ALIGN_DWORD;

      texture->pixel_depth = 32;
      texture->width  = line_length / 4;
      texture->height = screen_y;

      close(texture_fd);
    }

    
    // printf("smem_len: %d\n", f_info.smem_len);
    // printf("smem_len / virtual_screen_y: %d\n", f_info.smem_len / virtual_screen_y);
    // printf("linelength: %d\n", f_info.line_length);

    // printf("%d, %d: %d\n", screen_x, screen_y, fd_fb0);
    // printf("%d, %d: %d\n", virtual_screen_x, virtual_screen_y, fd_fb0);


    uint8_t* frame = mmap( 
      NULL, fb0_size, 
      PROT_READ | PROT_WRITE, 
      MAP_SHARED, fd_fb0, 0
    );

    cairo_surface_t *surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, 
      virtual_screen_x, 
      virtual_screen_y
    );
    cairo_t *cr = cairo_create(surface);

    
    // v_info.activate |= FB_ACTIVATE_FORCE;
    v_info.xoffset = 0;
    v_info.yoffset = 0;

    double app_start     = get_time_sec();

    double frame_start   = app_start;
    double frame_end     = 0;
    double frame_elapsed = 0;

    double draw_start   = 0;
    double draw_end     = 0;
    double draw_elapsed = 0;
    
    int crtc_number = 0;

    while (keep_running) {
      frame_start   = frame_end;
      frame_end     = get_time_sec();
      frame_elapsed = frame_end - frame_start;

      // process virtual terminal switches
      if (now_afk && was_afk) {
        usleep(100 * 1000);
      } else if (now_afk) {
        ioctl(fd_tty, VT_RELDISP, 1);
        was_afk = now_afk;
      } else if (was_afk) {
        ioctl(fd_tty, VT_RELDISP, VT_ACKACQ);
        was_afk = now_afk;
      }
      // be sure not to do anything
      // when we are afk or on transitions
      if (now_afk || was_afk) {
        continue;
      }

      draw_start   = get_time_sec();
      
      #define CAIRO_SET_HEX_RGBA(cr, hex) \
        cairo_set_source_rgba((cr), \
            (((hex) >>  0) & 0xFF) / 255.0, \
            (((hex) >>  8) & 0xFF) / 255.0, \
            (((hex) >> 16) & 0xFF) / 255.0, \
            (((hex) >> 24) & 0xFF) / 255.0)
      
      CAIRO_SET_HEX_RGBA(cr, cfg->rgba_background);
      cairo_paint(cr);

      double frame_elapsed_ms = frame_elapsed * 1000;
      double  draw_elapsed_ms =  draw_elapsed * 1000;
      char frame_text_buffer[64];
      char  draw_text_buffer[64];
      snprintf(frame_text_buffer, sizeof(frame_text_buffer), "Frame time: %.2f ms", frame_elapsed_ms);
      snprintf(draw_text_buffer,  sizeof(draw_text_buffer),  "Draw  time: %.2f ms",  draw_elapsed_ms);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 24.0);
      cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

      cairo_move_to(cr, 20.0, 50.0);
      cairo_show_text(cr, frame_text_buffer);

      cairo_move_to(cr, 20.0, 100.0);
      cairo_show_text(cr, draw_text_buffer);

      // Draw a red line
      cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
      cairo_set_line_width(cr, 5.0);
      
      cairo_move_to(cr, 10.0 + cfg->x, 10.0 + cfg->y);

      double x = sin((frame_end - app_start) / 1.0);
      cairo_line_to(cr, 110.0 * (x + 1), 110.0);
      cairo_stroke(cr);

      cairo_surface_flush(surface);
      uint8_t *ram_pixels = cairo_image_surface_get_data(surface);
      
      // memcpy(frame, ram_pixels, fb0_size);

      alpha_blend_memcpy(
        frame,
        texture->pixels,
        ram_pixels,
        fb0_size
      );

      draw_end     = get_time_sec();
      draw_elapsed = draw_end - draw_start;

      // note: in ideal world we should have called this then pan?!
      //       in reality it creates a double vsync on drm kms emulated fb0 driver
      // ioctl(fd_fb0, FBIO_WAITFORVSYNC, &crtc_number);
      ioctl(fd_fb0, FBIOPAN_DISPLAY, &v_info);
    }
  
    // cleanup
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return 0;
}

int main() {
  pid_t pid = fork(); // syscall(SYS_fork);
  
  if (pid == 0) {
    real_main();
    _exit(0);
  } else {
    signal(SIGINT, SIG_IGN);
    int status;
    waitpid(pid, &status, 0);
    int32_t fd_tty = open("/dev/tty0", O_RDWR);
    ioctl(fd_tty, KDSETMODE, KD_TEXT); 
  }
}
