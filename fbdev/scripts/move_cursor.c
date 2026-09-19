#define main undefined
#include ".root/fbdev/framebuffer.c"
#undef main 

int main() {
    signal(SIGINT, handle_sigint);

    int32_t pagesize = sysconf(_SC_PAGESIZE); // just make a page
    volatile struct config* cfg = config_quick_edit(".root/fbdev/data/config.bin", pagesize);

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
      
      cfg->x = mode_x - 500;
      cfg->y = mode_y - 500;
    }
}
