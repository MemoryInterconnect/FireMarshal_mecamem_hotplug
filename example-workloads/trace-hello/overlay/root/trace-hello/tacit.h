#ifndef TACIT_H
#define TACIT_H

#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>

#define TRACE_IOC_MAGIC      't'
#define TRACE_IOC_ENABLE     _IO(TRACE_IOC_MAGIC, 0)  /* arg: 1=on, 0=off */
#define TRACE_IOC_DISABLE    _IO(TRACE_IOC_MAGIC, 1)

static inline int tacit_open(void) {
  const char *devpath = "/dev/tacit0";
  return open(devpath, O_RDWR);
}

static inline int tacit_enable(int fd) {
  return ioctl(fd, TRACE_IOC_ENABLE);
}

static inline int tacit_disable(int fd) {
  return ioctl(fd, TRACE_IOC_DISABLE);
}

static inline int tacit_close(int fd) {
  return close(fd);
}

#endif