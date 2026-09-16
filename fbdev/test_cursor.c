#include <sys/mman.h>
#include <signal.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

struct config {
  int32_t x;
  int32_t y;
  uint32_t rgba_background;
};

volatile sig_atomic_t keep_running = 1;

void handle_sigint(int sig) {
    keep_running = 0; 
}

double get_time_sec(void) {
    struct timespec ts;
    // syscall(SYS_clock_gettime, CLOCK_MONOTONIC, &ts);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

int main() {

    signal(SIGINT, handle_sigint);


    // open up live config
    int32_t pagesize = sysconf(_SC_PAGESIZE); // just make a page
    volatile struct config* cfg = NULL;
    {
      int32_t config_fd = open("config.bin", O_RDWR);
      cfg = mmap(
          NULL, pagesize,
          PROT_READ | PROT_WRITE,
          MAP_SHARED, config_fd, 0
      );
      close(config_fd);
    }

    double start = get_time_sec();
    double now   = get_time_sec();
    double dt    = 0;
    double vector_size = 700;
    double vector_x = 1920 / 2;
    double vector_y = 1200 / 2;

    double point_x = vector_x;
    double point_y = vector_y;

    while(keep_running) 
    {
      dt = get_time_sec() - now;
      now += dt;
      double a = now - start;
      
      double mode_speed = 1;

      double upd_x = vector_x + cos(mode_speed * a) * vector_size;
      double upd_y = vector_y + sin(mode_speed * a) * vector_size;

      double mode_x = 0;
      double mode_y = 0;
      
      uint32_t mode_duration = 30 * mode_speed;
      uint32_t mode_circle   = 20 * mode_speed;
      uint32_t mode_horiz    = 10 * mode_speed;
      uint32_t mode_vert     = 0  * mode_speed;
      uint32_t itime         = a;
      if 
      (itime % mode_duration >= mode_circle) 
      {
        mode_x = upd_x;
        mode_y = upd_y;
      } 
      else if 
      (itime % mode_duration >= mode_horiz) 
      {
        mode_x = upd_x;
        mode_y = vector_y;
      } 
      else if 
      (itime % mode_duration >= mode_vert) 
      {
        mode_x = vector_x;
        mode_y = upd_y;
      }
      
      double speed = 1000;
      double dx = mode_x - point_x;
      double dy = mode_y - point_y;
      point_x += speed * dt * dx / sqrt(dx*dx + dy*dy);
      point_y += speed * dt * dy / sqrt(dx*dx + dy*dy);
      
      cfg->x = mode_x;
      cfg->y = mode_y;
    }
}
