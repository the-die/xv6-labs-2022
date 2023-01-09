#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]) {
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "pingpong: pipe failed\n");
    exit(1);
  }

  int ret = fork();
  if (ret == 0) {  // child
    // The following code has no error handling.
    close(p[0]);
    int pid = getpid();
    printf("%d: received ping\n", pid);
    write(p[1], "a", 1);
    close(p[1]);
  } else if (ret > 0) {  // parent
    // The following code has no error handling.
    close(p[1]);
    char c;
    read(p[0], &c, 1);
    if (c != 'a')
      fprintf(2, "pingpong: incorrect char read\n");
    close(p[0]);
    int pid = getpid();
    printf("%d: received pong\n", pid);
  } else {  // error
    fprintf(2, "pingpong: fork failed\n");
    exit(1);
  }

  exit(0);
}
