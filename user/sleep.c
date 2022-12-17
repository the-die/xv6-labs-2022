#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  if (argc <= 1) {
    fprintf(2, "usage: sleep ticks\n");
    exit(1);
  }

  int ticks = atoi(argv[1]);
  int ret = sleep(ticks);
  if (ret < 0) {
    fprintf(2, "sleep: failed\n");
    exit(1);
  }

  exit(0);
}
