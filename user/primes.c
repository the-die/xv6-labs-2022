#include "kernel/types.h"
#include "user/user.h"

int end = 36;

void primes(int fd);

// read, write and close without error handling
int main(int argc, char* argv[]) {
  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid == 0) {  // child
    close(p[1]);
    primes(p[0]);
    close(p[0]);
  } else if (pid > 0) {  // parent
    close(p[0]);
    for (int i = 2; i < end; ++i) {
      write(p[1], &i, sizeof(i));
    }
    close(p[1]);
    wait(0);
  } else {  // error
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }

  exit(0);
}

// read, write and close without error handling
void primes(int fd) {
  int n;
  if (read(fd, &n, sizeof(n)) == 0)
    return;
  printf("prime %d\n", n);

  int p[2];
  if (pipe(p) < 0) {
    fprintf(2, "primes: pipe failed\n");
    exit(1);
  }

  int pid = fork();
  if (pid == 0) {  // child
    close(p[1]);
    primes(p[0]);
    close(p[0]);
  } else if (pid > 0) {  // parent
    close(p[0]);
    int first = n;
    while (read(fd, &n, sizeof(n))) {
      if (n % first) {
        write(p[1], &n, sizeof(n));
      }
    }
    close(p[1]);
    wait(0);
  } else {  // error
    fprintf(2, "primes: fork failed\n");
    exit(1);
  }
}
