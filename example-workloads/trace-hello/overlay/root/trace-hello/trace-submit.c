#include "tacit.h"
#include <stdio.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>
#include <fcntl.h>

static void drain_tacit_log(int fd) {
  int flags = fcntl(fd, F_GETFL);
  if (flags >= 0 && !(flags & O_NONBLOCK)) {
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      perror("fcntl(O_NONBLOCK)");
      return;
    }
  }

  while (1) {
    struct tacit_log_record rec;
    ssize_t ret = read(fd, &rec, sizeof(rec));
    if (ret == (ssize_t)sizeof(rec)) {
      printf("tacit: asid=%u pid=%d comm=%.*s\n",
             rec.asid, rec.pid,
             TACIT_COMM_LEN, rec.comm);
      continue;
    }
    if (ret < 0) {
      if (errno == EAGAIN)
        break;
      perror("read");
      break;
    }
    if (ret == 0) {
      break;
    }
    fprintf(stderr, "short read from tacit log (%zd bytes)\n", ret);
    break;
  }
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: trace-submit <command> [args...]\n"); return 2; }

  int fd = tacit_open();
  if (fd < 0) {
    fprintf(stderr, "failed to open /dev/tacit0\n");
    return 1;
  }

  if (tacit_enable(fd) < 0) {
      fprintf(stderr, "failed to enable tacit\n");
      return 1;
  }
  pid_t pid = fork();
  if (pid < 0) {
    fprintf(stderr, "failed to fork\n");
    return 1;
  }
  if (pid == 0) {
    execvp(argv[1], &argv[1]);
    perror("execvp");
    return 127;
  }
  // parent
  int status;
  waitpid(pid, &status, 0);
  if (tacit_disable(fd) < 0) {
    fprintf(stderr, "failed to disable tacit\n");
    return 1;
  }
  drain_tacit_log(fd);
  if (tacit_close(fd) < 0) {
    fprintf(stderr, "failed to close /dev/tacit0\n");
    return 1;
  }
  return 0;
}
