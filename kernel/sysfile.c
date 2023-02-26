//
// File-system system calls.
// Mostly argument checking, since we don't trust
// user code, and calls into file.c and fs.c.
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"

// Fetch the nth word-sized system call argument as a file descriptor
// and return both the descriptor and the corresponding struct file.
static int argfd(int n, int *pfd, struct file **pf) {
  int fd;
  struct file *f;

  argint(n, &fd);
  if (fd < 0 || fd >= NOFILE || (f = myproc()->ofile[fd]) == 0)
    return -1;
  if (pfd)
    *pfd = fd;
  if (pf)
    *pf = f;
  return 0;
}

// Allocate a file descriptor for the given file.
// Takes over file reference from caller on success.
static int fdalloc(struct file *f) {
  int fd;
  struct proc *p = myproc();

  for (fd = 0; fd < NOFILE; fd++) {
    if (p->ofile[fd] == 0) {
      p->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64 sys_dup(void) {
  struct file *f;
  int fd;

  if (argfd(0, 0, &f) < 0)
    return -1;
  if ((fd = fdalloc(f)) < 0)
    return -1;
  filedup(f);
  return fd;
}

uint64 sys_read(void) {
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return fileread(f, p, n);
}

uint64 sys_write(void) {
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if (argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64 sys_close(void) {
  int fd;
  struct file *f;

  if (argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->ofile[fd] = 0;
  fileclose(f);
  return 0;
}

uint64 sys_fstat(void) {
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if (argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// Create the path new as a link to the same inode as old.
uint64 sys_link(void) {
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if (argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((ip = namei(old)) == 0) {
    end_op();
    return -1;
  }

  ilock(ip);
  if (ip->type == T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if ((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// Is the directory dp empty except for "." and ".." ?
static int isdirempty(struct inode *dp) {
  int off;
  struct dirent de;

  for (off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
    if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if (de.inum != 0)
      return 0;
  }
  return 1;
}

uint64 sys_unlink(void) {
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if (argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if ((dp = nameiparent(path, name)) == 0) {
    end_op();
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if (namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if ((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if (ip->nlink < 1)
    panic("unlink: nlink < 1");
  if (ip->type == T_DIR && !isdirempty(ip)) {
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if (ip->type == T_DIR) {
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode *create(char *path, short type, short major, short minor) {
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if ((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if ((ip = dirlookup(dp, name, 0)) != 0) {
    iunlockput(dp);
    ilock(ip);
    if (type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE))
      return ip;
    iunlockput(ip);
    return 0;
  }

  if ((ip = ialloc(dp->dev, type)) == 0) {
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  ip->nlink = 1;
  iupdate(ip);

  if (type == T_DIR) { // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if (dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if (type == T_DIR) {
    // now that success is guaranteed:
    dp->nlink++; // for ".."
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64 sys_open(void) {
  char path[MAXPATH];
  int fd, omode;
  struct file *f;
  struct inode *ip;
  int n;

  argint(1, &omode);
  if ((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  if (omode & O_CREATE) {
    ip = create(path, T_FILE, 0, 0);
    if (ip == 0) {
      end_op();
      return -1;
    }
  } else {
    if ((ip = namei(path)) == 0) {
      end_op();
      return -1;
    }
    ilock(ip);
    if (ip->type == T_DIR && omode != O_RDONLY) {
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if (ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)) {
    iunlockput(ip);
    end_op();
    return -1;
  }

  if ((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0) {
    if (f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if (ip->type == T_DEVICE) {
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);

  if ((omode & O_TRUNC) && ip->type == T_FILE) {
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64 sys_mkdir(void) {
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64 sys_mknod(void) {
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if ((argstr(0, path, MAXPATH)) < 0 ||
      (ip = create(path, T_DEVICE, major, minor)) == 0) {
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64 sys_chdir(void) {
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();

  begin_op();
  if (argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0) {
    end_op();
    return -1;
  }
  ilock(ip);
  if (ip->type != T_DIR) {
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  iput(p->cwd);
  end_op();
  p->cwd = ip;
  return 0;
}

uint64 sys_exec(void) {
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if (argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  memset(argv, 0, sizeof(argv));
  for (i = 0;; i++) {
    if (i >= NELEM(argv)) {
      goto bad;
    }
    if (fetchaddr(uargv + sizeof(uint64) * i, (uint64 *)&uarg) < 0) {
      goto bad;
    }
    if (uarg == 0) {
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if (argv[i] == 0)
      goto bad;
    if (fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = exec(path, argv);

  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

bad:
  for (i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

uint64 sys_pipe(void) {
  uint64 fdarray; // user pointer to array of two integers
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  argaddr(0, &fdarray);
  if (pipealloc(&rf, &wf) < 0)
    return -1;
  fd0 = -1;
  if ((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0) {
    if (fd0 >= 0)
      p->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if (copyout(p->pagetable, fdarray, (char *)&fd0, sizeof(fd0)) < 0 ||
      copyout(p->pagetable, fdarray + sizeof(fd0), (char *)&fd1, sizeof(fd1)) <
          0) {
    p->ofile[fd0] = 0;
    p->ofile[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

/*
 * void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t
 * offset);
 *
 * @addr: You can assume that addr will always be zero, meaning that the kernel
 * should decide the virtual address at which to map the file. mmap returns that
 * address, or 0xffffffffffffffff if it fails.
 *
 * @length: length is the number of bytes to map; it might not be the same as
 * the file's length.
 *
 * @prot: prot indicates whether the memory should be mapped readable,
 * writeable, and/or executable; you can assume that prot is PROT_READ or
 * PROT_WRITE or both.
 *
 * @flags: flags will be either MAP_SHARED, meaning that modifications to the
 * mapped memory should be written back to the file, or MAP_PRIVATE, meaning
 * that they should not.
 *
 * @fd: fd is the open file descriptor of the file to map.
 *
 * @offset: You can assume offset is zero (it's the starting point in the file
 * at which to map).
 */
uint64 sys_mmap(void) {
  void *addr;
  uint64 length;
  int prot;  // PROT_READ or PROT_WRITE or both
  int flags; // shared or private
  int fd;
  struct file *file;
  uint64 offset;
  argaddr(0, (void *)&addr); // ignored
  argint64(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argfd(4, &fd, &file);
  argint64(5, &offset); // ignored

  if (file->readable == 0 && (prot & PROT_READ)) {
    printf("mmap: invalid read operation\n");
    return -1;
  }
  if (file->writable == 0 && (prot & PROT_WRITE) && (flags & MAP_SHARED)) {
    printf("mmap: invalid write operation\n");
    return -1;
  }

  struct proc *p = myproc();
  for (int i = 0; i < NVMA; ++i) {
    if (p->vmas[i].used)
      continue;

    addr = (void *)PGROUNDDOWN(p->mmap_top - length);
    p->vmas[i].used = 1;
    p->vmas[i].addr = addr;
    p->vmas[i].length = length;
    p->vmas[i].prot = prot;
    p->vmas[i].flags = flags;
    p->vmas[i].fd = fd;
    p->vmas[i].file = file;
    p->vmas[i].offset = offset;
    p->mmap_top = (uint64)addr;
    filedup(file);
    return (uint64)addr;
  }
  return -1;
}

/*
 * int munmap(void *addr, size_t length)
 *
 * munmap(addr, length) should remove mmap mappings in the indicated address
 * range. If the process has modified the memory and has it mapped MAP_SHARED,
 * the modifications should first be written to the file. An munmap call might
 * cover only a portion of an mmap-ed region, but you can assume that it will
 * either unmap at the start, or at the end, or the whole region (but not punch
 * a hole in the middle of a region).
 */
uint64 sys_munmap(void) {
  void *addr;
  uint64 length;
  argaddr(0, (void *)&addr);
  argint64(1, &length);

  uint64 begin = PGROUNDDOWN((uint64)addr);
  uint64 end = PGROUNDUP((uint64)addr + length);
  struct proc *p = myproc();
  struct vma *vma = 0;
  for (int i = 0; i < NVMA; ++i) {
    if (p->vmas[i].used == 0)
      continue;

    uint64 vma_end = PGROUNDUP((uint64)p->vmas[i].addr + p->vmas[i].length);
    if (begin >= (uint64)p->vmas[i].addr && end <= vma_end) {
      vma = &p->vmas[i];
      break;
    }
  }

  if (vma == 0) {
    printf("munmap: invalid range %p, %p\n", begin, end);
    return -1;
  }

  struct file *file = vma->file;
  if (vma->flags & MAP_SHARED) {
    if (file->writable == 0) {
      printf("munmap: the file is unwritable\n");
      return -1;
    }
    for (uint n = 0; n < end - begin; n += PGSIZE) {
      pte_t *pte = walk(p->pagetable, begin + n, 0);
      if (pte == 0)
        panic("munmap: walk");
      if ((*pte & PTE_V) == 0)
        continue;
      begin_op();
      ilock(file->ip);
      // writei call iupdate
      if (writei(file->ip, 1, begin + n,
                 begin + n - (uint64)vma->addr + vma->offset,
                 PGSIZE) != PGSIZE) {
        iunlockput(file->ip);
        end_op();
        return -1;
      }
      iunlockput(file->ip);
      end_op();
    }
  }

  for (uint64 i = begin; i < end; i += PGSIZE) {
    pte_t *pte = walk(p->pagetable, i, 0);
    if (pte == 0)
      panic("munmap: walk");
    if ((*pte & PTE_V) == 0)
      continue;
    uvmunmap(p->pagetable, i, 1, 1);
  }

  uint64 vma_end = PGROUNDUP((uint64)vma->addr + vma->length);
  if (begin == (uint64)vma->addr && end == vma_end) {
    vma->used = 0;
    filered(file);
  } else if (begin == (uint64)vma->addr && end < vma_end) {
    vma->addr = (void *)end;
    vma->length -= end - begin;
    vma->offset += end - begin;
  } else if (begin > (uint64)vma->addr && end == vma_end) {
    vma->length = begin - (uint64)vma->addr;
  } else {
    panic("munmap: range not supported");
  }

  return 0;
}
