#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "user/signal.h"

int main(int argc, char **argv){
  if(argc < 3){
    fprintf(2, "usage: sigsend pid signum\n");
    exit(1);
  }
  int pid = atoi(argv[1]);
  int sig = atoi(argv[2]);
  int r = sigsend(pid, sig);
  if(r < 0){
    fprintf(2, "sigsend: failed to send %d to pid %d\n", sig, pid);
    exit(1);
  }
  printf("sigsend: sent %d to pid %d\n", sig, pid);
  exit(0);
}