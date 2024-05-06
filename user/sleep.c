#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char* argv[])
{
  int sleep_num;
  if (argc != 2) {
    fprintf(2, "Usage: sleep num...\n");
    exit(1);
  }

  sleep_num = atoi(argv[1]);
  sleep(sleep_num);

  exit(0);
}
