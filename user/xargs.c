#include "kernel/types.h"
#include "kernel/param.h"
#include "user/user.h"

#define is_blank(c) ((c) == ' ' || (c) == '\t')

static void runcmd(const char* cmd, char* args[]) {
  int pid = fork();
  if (pid == 0) {  // child
    exec(cmd, args);
    fprintf(2, "xargs: exec failed\n");
    exit(1);
  } else if (pid > 0) {  // parent
    wait(0);
  } else {  // error
    fprintf(2, "xargs: fork failed\n");
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(2, "usage: xargs <command> [options...]\n");
    exit(1);
  }

  char* cmd = argv[1];
  char* args[MAXARG];
  for (int i = 0; i < argc - 1; ++i) {
    args[i] = argv[i + 1];
  }

  char c;
  char buf[1024];
  int blank_cnt = 0;
  int i = 0;
  int j = argc - 1;
  char* p = buf;
  while (read(0, &c, 1)) {
    if (is_blank(c)) {
      ++blank_cnt;
      continue;
    }

    if (blank_cnt) {
      buf[i++] = '\0';
      args[j++] = p;
      p = buf + i;
      blank_cnt = 0;
    }

    if (c == '\n') {
      buf[i++] = '\0';
      args[j++] = p;
      p = buf + i;
      j = argc - 1;
      runcmd(cmd, args);
    } else {
      buf[i++] = c;
    }
  }

  exit(0);
}
