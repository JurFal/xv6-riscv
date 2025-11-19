#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  printf("Initiating system shutdown...\n");
  
  // Call the new shutdown system call
  shutdown();
  
  // This line should not be reached if shutdown works correctly
  printf("Error: shutdown system call returned unexpectedly\n");
  exit(1);
}