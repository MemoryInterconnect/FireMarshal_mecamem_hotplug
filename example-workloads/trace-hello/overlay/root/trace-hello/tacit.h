#ifndef TACIT_H
#define TACIT_H

#include <sys/ioctl.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>

#define TACIT_COMM_LEN 16

struct tacit_log_record {
  uint32_t asid;
  pid_t pid;
  char comm[TACIT_COMM_LEN];
};

#define TACIT_LOG_RECORD_SIZE ((ssize_t)sizeof(struct tacit_log_record))

#define TRACE_IOC_MAGIC      't'
// --- IOCTL commands ---
// Enable the trace encoder
#define TRACE_IOC_ENABLE     _IO(TRACE_IOC_MAGIC, 0)
// Disable the trace encoder
#define TRACE_IOC_DISABLE    _IO(TRACE_IOC_MAGIC, 1)
// Set the context mode for the trace encoder
#define TRACE_IOC_WATCH_PID   _IO(TRACE_IOC_MAGIC, 2)

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

static inline int tacit_watch_pid(int fd) {
  return ioctl(fd, TRACE_IOC_WATCH_PID);
}

static inline int tacit_close(int fd) {
  return close(fd);
}

#endif
