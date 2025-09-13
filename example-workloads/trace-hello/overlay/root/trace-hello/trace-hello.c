#include "tacit.h"
#include <stdio.h>

int main(void) {
  int fd = tacit_open();
  if (fd < 0) {
    fprintf(stderr, "failed to open /dev/tacit0\n");
    return 1;
  }
  int ret = tacit_enable(fd);
  if (ret < 0) {
    fprintf(stderr, "failed to enable tacit\n");
    return 1;
  }
  printf("Hello, world!\n");
  ret = tacit_disable(fd);
  if (ret < 0) {
    fprintf(stderr, "failed to disable tacit\n");
    return 1;
  }
  ret = tacit_close(fd);
  if (ret < 0) {
    fprintf(stderr, "failed to close /dev/tacit0\n");
    return 1;
  }
  return 0;
}