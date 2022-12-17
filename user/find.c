#include "kernel/types.h"
#include "kernel/fs.h"
#include "kernel/fcntl.h"
#include "kernel/stat.h"
#include "user/user.h"

static char buf[512];

static const char* basename(const char* path) {
  const char* p = path + strlen(path);
  for (; p >= path && *p != '/'; --p)
    continue;
  ++p;
  return p;
}

static void find(const char* path, const char* file) {
  int fd;
  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(2, "find: open %s failed\n", path);
    exit(1);
  }

  struct stat st;
  if (fstat(fd, &st) < 0) {
    fprintf(2, "find: fstat %s failed\n", path);
    exit(1);
  }

  struct dirent de;
  switch (st.type) {
    case T_DEVICE:
    case T_FILE:
      break;
    case T_DIR:
      if (strlen(path) + 1 + DIRSIZ + 1 > sizeof(buf)) {
        fprintf(2, "find: path %s too long\n", path);
        exit(1);
      }
      strcpy(buf, path);
      char* p = buf + strlen(buf);
      *p++ ='/';
      while (read(fd, &de, sizeof(de)) == sizeof(de)) {
        if (de.inum == 0)
          continue;
        if (strcmp(basename(de.name), ".") == 0)
          continue;
        if (strcmp(basename(de.name), "..") == 0)
          continue;
        // strcpy(p, de.name);
        memmove(p, de.name, DIRSIZ);
        p[DIRSIZ] = 0;
        if (strcmp(de.name, file) == 0)
          printf("%s\n", buf);
        find(buf, file);
      }
      break;
    default:
      fprintf(2, "find: stat type %d error\n", st.type);
      exit(1);
  }
  close(fd);
}

int main(int argc, char* argv[]) {
  if (argc < 2) {
    fprintf(2, "usage: find [path] file\n");
    exit(1);
  }
  if (argc == 2)
    find(".", argv[1]);
  else
    find(argv[1], argv[2]);
  exit(0);
}
