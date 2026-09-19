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

#include <stdlib.h>
#include <assert.h>

#define max(a, b) (((a) > (b))? (a) : (b))
#define min(a, b) (((a) < (b))? (a) : (b))

#define interpret(type_name, ptr, ...) \
    struct __attribute__((packed, may_alias)) type_name { __VA_ARGS__ } *type_name = \
    (struct type_name *)(ptr)

int32_t fd_tty;

volatile sig_atomic_t keep_running = 1;
volatile sig_atomic_t now_afk = 0;
volatile sig_atomic_t was_afk = 0;

uint64_t get_rdtsc() {
  uint64_t result;
  interpret(view, &result, 
    uint32_t lo; 
    uint32_t hi;
  );
  asm volatile (
    "rdtsc" : 
      "=a" (view->lo), 
      "=d" (view->hi)
  );
  return result;
}

void handle_sigint(int sig) {
    keep_running = 0; 
}

double get_time_sec(void) {
    struct timespec ts;
    // syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

struct config {
  int32_t x;
  int32_t y;
  uint32_t rgba_background;
};

#define TGA_ALIGN 0
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
  uint8_t  _[TGA_ALIGN];
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
    uint8_t b_b = b >> 0;

    uint32_t alpha   = a_a;
    uint32_t i_alpha = 0xFF - alpha;
    
    // review this trick and find out why this works
    // but keep for now as is
    //
    // proudly stolen from here: 
    // https://arxiv.org/pdf/2202.02864
    //
    uint8_t d_a = 0xFF;
    uint8_t d_r = ((a_r * alpha) + (b_r * i_alpha) + 128) >> 8;
    uint8_t d_g = ((a_g * alpha) + (b_g * i_alpha) + 128) >> 8;
    uint8_t d_b = ((a_b * alpha) + (b_b * i_alpha) + 128) >> 8;

    d |= (d_a << 24) 
      |  (d_r << 16) 
      |  (d_g << 8)  
      |  (d_b << 0);

    color_d[i] = d;
  }
}

uint32_t tga_texture_size(const struct tga_texture* texture)
{
  uint32_t width       = texture->width;
  uint32_t height      = texture->height;
  uint32_t pixel_depth = texture->pixel_depth;

  return width * height * (pixel_depth / 8);
}

void alpha_blend_quad_memcpy
  (void* restrict bytes_d,
   void* restrict bytes_a, 
   int32_t d_width,      int32_t d_height,
   int32_t a_width,      int32_t a_height,
   int32_t d_quad_x,     int32_t d_quad_y,
   int32_t a_quad_x,     int32_t a_quad_y,
   int32_t quad_width,   int32_t quad_height
   )
{
  uint32_t* color_a = bytes_a;
  uint32_t* color_d = bytes_d;

  assert(d_width     > 0);
  assert(d_height    > 0);
  assert(a_width     > 0);
  assert(a_height    > 0);
  assert(quad_width  > 0);
  assert(quad_height > 0);
  
  // NOTE(ivan): this must be signed, since if sprite is out of the screen
  //             the height and width would be negative
  int32_t a_quad_height   = min(a_quad_y + quad_height, a_height) - a_quad_y;
  int32_t a_quad_width    = min(a_quad_x + quad_width,  a_width ) - a_quad_x;
  int32_t d_quad_height   = min(d_quad_y + quad_height, d_height) - d_quad_y;
  int32_t d_quad_width    = min(d_quad_x + quad_width,  d_width ) - d_quad_x;

  int32_t vis_quad_height = min(a_quad_height, d_quad_height);
  int32_t vis_quad_width  = min(a_quad_width,  d_quad_width );

  // NOTE(ivan): this can go further than vis_quad_width
  //             but that's okay
  int32_t vis_quad_y      = max(max(-a_quad_y, -d_quad_y), 0);
  int32_t vis_quad_x      = max(max(-a_quad_x, -d_quad_x), 0);
  
  for (int32_t y = vis_quad_y; y < vis_quad_height; y++) {
    for (int32_t x = vis_quad_x; x < vis_quad_width; x++) {

      int32_t a_x = a_quad_x + x;
      int32_t a_y = a_quad_y + y;

      int32_t d_x = d_quad_x + x;
      int32_t d_y = d_quad_y + y;
      
      // otherwise 
      {
        uint32_t a_flat_index = a_x + a_y * a_width;
        uint32_t d_flat_index = d_x + d_y * d_width;
        
        uint32_t a = color_a[a_flat_index];
        uint32_t d = color_d[d_flat_index];
        uint32_t r = 0;

        uint8_t a_a = a >> 24;
        uint8_t a_r = a >> 16;
        uint8_t a_g = a >> 8;
        uint8_t a_b = a >> 0;

        uint8_t d_a = d >> 24;
        uint8_t d_r = d >> 16;
        uint8_t d_g = d >> 8;
        uint8_t d_b = d >> 0;

        uint32_t alpha   = a_a;
        uint32_t i_alpha = 0xFF - alpha;
        
        // review this trick and find out why this works
        // but keep for now as is
        //
        // proudly stolen from here: 
        // https://arxiv.org/pdf/2202.02864
        //
        uint8_t r_a = 0xFF;
        uint8_t r_r = ((a_r * alpha) + (d_r * i_alpha) + 128) >> 8;
        uint8_t r_g = ((a_g * alpha) + (d_g * i_alpha) + 128) >> 8;
        uint8_t r_b = ((a_b * alpha) + (d_b * i_alpha) + 128) >> 8;

        r |= (r_a << 24) 
          |  (r_r << 16) 
          |  (r_g << 8)  
          |  (r_b << 0);

        color_d[d_flat_index] = r;
      }
    }
  }
}

struct tga_texture* tga_quick_map(const char* mapping_path, uint16_t width, uint16_t height)
{
  struct tga_texture* texture = NULL;

  uint32_t permissions = 0777; // ignore permissions issues for now
  uint32_t width_32    = width;
  uint32_t height_32   = height;
  uint8_t  pixel_depth = 32;
  uint8_t  pixel_depth_bytes = pixel_depth / 8;

  uint32_t texture_size = sizeof(struct tga_texture) + width_32 * height_32 * pixel_depth_bytes;
  {
    int result_open = open(mapping_path, O_RDWR | O_CREAT, permissions);
    if (result_open >= 0) 
    {
      int texture_fd = result_open;
      int result_chmod = fchmod(texture_fd, permissions);
      if (result_chmod >= 0) 
      {
        int result_truncate = ftruncate(texture_fd, texture_size);
        if (result_truncate >= 0) 
        {
          void* result_mmap = mmap(
            NULL, texture_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED, texture_fd, 0
          );
          if (result_mmap != MAP_FAILED) 
          {
            texture = result_mmap;

            texture->image_type        |= TGA_IT_UNCOMPRESSED_TRUE_COLOR;
            texture->image_description |= TGA_ID_ALPHA_CHANNEL;
            texture->image_description |= TGA_ID_FROM_TOP_LEFT;

            texture->image_id = TGA_ALIGN;

            texture->pixel_depth = pixel_depth;
            texture->width  = width;
            texture->height = height;
          }
        } 
      }
      close(result_open);
    }
  }
  return texture;
}

struct tga_texture* tga_quick_edit(const char* mapping_path, uint16_t width, uint16_t height)
{
  struct tga_texture* texture = NULL;

  uint32_t width_32    = width;
  uint32_t height_32   = height;
  uint8_t  pixel_depth = 32;
  uint8_t  pixel_depth_bytes = pixel_depth / 8;

  uint32_t texture_size = sizeof(struct tga_texture) + width_32 * height_32 * pixel_depth_bytes;
  {
    int result_open = open(mapping_path, O_RDWR);
    if (result_open >= 0) 
    {
      int texture_fd = result_open;
      void* result_mmap = mmap(
        NULL, texture_size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED, texture_fd, 0
      );
      if (result_mmap != MAP_FAILED) 
      {
        texture = result_mmap;

        texture->image_type        |= TGA_IT_UNCOMPRESSED_TRUE_COLOR;
        texture->image_description |= TGA_ID_ALPHA_CHANNEL;
        texture->image_description |= TGA_ID_FROM_TOP_LEFT;

        texture->image_id = TGA_ALIGN;

        texture->pixel_depth = pixel_depth;
        texture->width  = width;
        texture->height = height;
      }
      close(result_open);
    }
  }
  return texture;
}

struct config* config_quick_edit(const char* name, uint32_t size)
{
  struct config* config = NULL;
  {
    int config_fd = open(name, O_RDWR);
    if (config_fd >= 0) 
    {
      void* mmap_result = mmap(
        NULL, size,
        PROT_READ | PROT_WRITE,
        MAP_SHARED, config_fd, 0
      );
      if (mmap_result != MAP_FAILED) 
      {
        config = mmap_result;
      }
      close(config_fd);
    }
  }
  return config;
}

struct config* config_quick_map(const char* name, uint32_t size) 
{
  int32_t permissions = 0777; // ignore permissions issues for now
  
  struct config* config = NULL;
  {
    int config_fd = open(name, O_RDWR | O_CREAT, permissions);
    if (config_fd >= 0) 
    {
      int chmod_result = fchmod(config_fd, permissions);
      if (chmod_result >= 0) 
      {
        int truncate_result = ftruncate(config_fd, size);
        if (truncate_result >= 0) 
        {
          void* mmap_result = mmap(
            NULL, size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED, config_fd, 0
          );
          if (mmap_result != MAP_FAILED) 
          {
            config = mmap_result;
          }
        }
      }
      close(config_fd);
    }
  }
  return config;
}

int real_main(int fd_tty) {
    int32_t pagesize = sysconf(_SC_PAGESIZE); // just make a page
    volatile struct config* cfg = config_quick_map(".root/fbdev/data/config.bin", pagesize);

    int32_t fd_fb0 = open("/dev/fb0",  O_RDWR);

    struct fb_var_screeninfo v_info;
    struct fb_fix_screeninfo f_info;

    ioctl(fd_fb0, FBIOGET_VSCREENINFO, &v_info);
    ioctl(fd_fb0, FBIOGET_FSCREENINFO, &f_info);

    // read screen sizes  
    int32_t screen_x = v_info.xres;
    int32_t screen_y = v_info.yres;

    int32_t virtual_screen_x = v_info.xres_virtual;
    int32_t virtual_screen_y = v_info.yres_virtual;

    int32_t fb0_size = f_info.smem_len;
    int32_t line_length = f_info.line_length;


    // open up live texture
    // note: pretty much copypasted from above
    struct tga_texture* sketch = tga_quick_map(".root/fbdev/data/texture.tga", screen_x, screen_y);
    uint32_t sketch_size = tga_texture_size(sketch);

    struct tga_texture* ui_elements = tga_quick_map(".root/fbdev/data/ui.tga", 2048, 2048);
    uint32_t ui_elements_size = tga_texture_size(ui_elements);

    uint32_t* frame_ram = calloc(1, fb0_size);

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

    uint64_t draw_cycles_start   = 0;
    uint64_t draw_cycles_end     = 0;
    uint64_t draw_cycles_elapsed = 0;
    
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

      draw_cycles_start = get_rdtsc();
      draw_start = get_time_sec();

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

      char   frame_text_buffer[64];
      char    draw_text_buffer[64];
      char  cycles_text_buffer[64];

      snprintf(frame_text_buffer,   sizeof(frame_text_buffer), "Frame time: %.2f ms",  frame_elapsed_ms   );
      snprintf(draw_text_buffer,    sizeof(draw_text_buffer),  "Draw  time: %.2f ms",  draw_elapsed_ms    );
      snprintf(cycles_text_buffer,  sizeof(draw_text_buffer),  "    Cycles: %lu c  ",  draw_cycles_elapsed);

      cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
      cairo_set_font_size(cr, 24.0);
      cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);

      cairo_move_to(cr, 20.0, 50.0);
      cairo_show_text(cr, frame_text_buffer);

      cairo_move_to(cr, 20.0, 100.0);
      cairo_show_text(cr, draw_text_buffer);

      cairo_move_to(cr, 20.0, 150.0);
      cairo_show_text(cr, cycles_text_buffer);

      // Draw a red line
      cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
      cairo_set_line_width(cr, 5.0);
      
      cairo_move_to(cr, 310.0, 110.0);

      double x = sin((frame_end - app_start) / 1.0);
      cairo_line_to(cr, 110.0 * (x + 1), 110.0);
      cairo_stroke(cr);

      cairo_surface_flush(surface);
      uint8_t *ram_pixels = cairo_image_surface_get_data(surface);
      
      // memcpy(frame, ram_pixels, fb0_size);

      alpha_blend_memcpy(
        frame_ram,
        sketch->pixels,
        ram_pixels,
        fb0_size
      );

      // draw cursor
      alpha_blend_quad_memcpy(
        frame_ram,
        ui_elements->pixels,
        screen_x, screen_y,
        ui_elements->width, ui_elements->height,
        cfg->x, cfg->y,
        0, 0,
        128, 128
      );

      memcpy(frame, frame_ram, fb0_size);

      draw_cycles_end = get_rdtsc();
      draw_cycles_elapsed = draw_cycles_end - draw_cycles_start;
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
  int32_t fd_tty = open("/dev/tty0", O_RDWR);
  int            saved_kd_mode;
  struct vt_mode saved_vt_mode;
  ioctl(fd_tty, VT_GETMODE, &saved_vt_mode);
  ioctl(fd_tty, KDGETMODE,  &saved_kd_mode);

  pid_t pid = fork();

  if (pid == 0) {
    // stop when I request it
    signal(SIGINT, handle_sigint);

    // allow switching between virtual terminals
    signal(SIGUSR1, virtual_terminal_switch);
    signal(SIGUSR2, virtual_terminal_switch);

    struct vt_mode mode = {
        .mode   = VT_PROCESS,
        .relsig = SIGUSR1,
        .acqsig = SIGUSR2
    };

    // tell us when vt switch happens
    if (ioctl(fd_tty, VT_SETMODE, &mode) != 0) { 
      perror("VT_SETMODE");
      abort();
    }
    // tell system not to display fbcon
    if (ioctl(fd_tty, KDSETMODE, KD_GRAPHICS) != 0) {
      perror("KD_GRAPHICS");
      abort();
    }

    real_main(fd_tty);
    
    ioctl(fd_tty, KDSETMODE,   saved_kd_mode); 
    ioctl(fd_tty, VT_SETMODE, &saved_vt_mode); 
    return 0;
  } else {
    signal(SIGINT, SIG_IGN);

    int status;
    waitpid(pid, &status, 0);

    ioctl(fd_tty, KDSETMODE,   saved_kd_mode); 
    ioctl(fd_tty, VT_SETMODE, &saved_vt_mode); 

    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    }
    return 1;
  }
}
