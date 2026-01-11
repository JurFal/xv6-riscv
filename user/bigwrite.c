#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

int
main(void)
{
  int fd = open("big", O_CREATE | O_WRONLY);
  if (fd < 0) {
    printf("bigwrite: open failed\n");
    exit(1);
  }

  static char buf[1024];
  for (int i = 0; i < 3000; i++) {
    if (write(fd, buf, sizeof(buf)) != sizeof(buf)) {
      printf("bigwrite: write failed at i=%d\n", i);
      exit(1);
    }
  }

  close(fd);
  printf("bigwrite: ok\n");
  exit(0);
}
