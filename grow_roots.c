#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int main() {
  char* command = "find . " 
      /* search for directories only   */ "-type d "
      /* but not for symlinks          */ "-not -type l "
      /* exclude .clang, .git etc      */ "-not -path '*/.*' " 
      /* print this crap as depth, path*/ "-printf '%d %p\n'";

  FILE* file = popen(command, "r");
  if (file == NULL) {
    perror("find");
    abort();
  }
  
  char*  line = NULL;
  size_t line_length = 0;
  ssize_t read = 0;

  const char* link_name = ".root";
  int link_name_space = strlen(link_name) + 1;

  while ((read = getline(&line, &line_length, file)) > 0)
  {
    int depth;
    char* path      = malloc(read);
    char* link_path = malloc(read + link_name_space + 1);

    if (sscanf(line, "%d %s", &depth, path) == 2) {
      sprintf(link_path, "%s/%s", path, link_name);

      char* highway_to_hell = calloc(1, (depth + 1) * 3);
      highway_to_hell[0] = '.';
      highway_to_hell[1] = '/';
      for (int i = 0; i < depth * 3; i += 3) {
        highway_to_hell[i + 0] = '.';
        highway_to_hell[i + 1] = '.';
        highway_to_hell[i + 2] = '/';
      }

      unlink(link_path);

      if (symlink(highway_to_hell, link_path) == 0) {
        printf("%s -> %s\n", link_path, highway_to_hell);
      } else {
        perror("symlink");
        abort();
      }
    }
  }

}
