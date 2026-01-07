#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/fcntl.h"

#define MAXPATH 128

static void
basic(void)
{
  int fd = open("real", O_CREATE | O_WRONLY);
  if (fd < 0) { printf("symlinktest: open real failed\n"); exit(1); }
  write(fd, "hello", 5);
  close(fd);

  if (symlink("real", "mylink") < 0) { printf("symlinktest: symlink failed\n"); exit(1); }

  fd = open("mylink", O_RDONLY);
  if (fd < 0) { printf("symlinktest: open link failed\n"); exit(1); }
  char buf[6] = {0};
  read(fd, buf, 5);
  close(fd);

  if (strcmp(buf, "hello") != 0) {
    printf("symlinktest: expect hello, got %s\n", buf);
    exit(1);
  }
}

static void
dangling(void)
{
  if (symlink("no_such_target", "dangling") < 0) {
    printf("symlinktest: dangling create failed\n");
    exit(1);
  }
  int fd = open("dangling", O_RDONLY);
  if (fd >= 0) {
    printf("symlinktest: dangling open should fail\n");
    close(fd);
    exit(1);
  }
}

static void
circular(void)
{
  if(symlink("c1", "c2") < 0) {
      printf("symlinktest: circular create failed\n");
      exit(1);
  }
  if(symlink("c2", "c1") < 0) {
      printf("symlinktest: circular create failed\n");
      exit(1);
  }
  int fd = open("c1", O_RDONLY);
  if(fd >= 0) {
      printf("symlinktest: circular open should fail\n");
      close(fd);
      exit(1);
  }
}

static void
nofollow(void)
{
    if(symlink("real", "link2") < 0) {
        printf("symlinktest: nofollow create failed\n");
        exit(1);
    }
    int fd = open("link2", O_NOFOLLOW | O_RDONLY);
    if(fd < 0) {
        printf("symlinktest: nofollow open failed\n");
        exit(1);
    }
    // Optional: read and verify it is the path "real"
    // But since O_NOFOLLOW opens the symlink, we can read its content.
    char buf[MAXPATH];
    int n = read(fd, buf, sizeof(buf));
    if(n <= 0) {
        printf("symlinktest: nofollow read failed\n");
        exit(1);
    }
    // buf should be "real"
    if(strcmp(buf, "real") != 0) {
        printf("symlinktest: nofollow expect 'real', got '%s'\n", buf);
        exit(1);
    }
    close(fd);
}

static void
cleanup(void)
{
  unlink("real");
  unlink("mylink");
  unlink("dangling");
  unlink("c1");
  unlink("c2");
  unlink("link2");
}

int
main(void)
{
  cleanup();
  basic();
  dangling();
  circular();
  nofollow();
  printf("symlinktest: ok\n");
  exit(0);
}
