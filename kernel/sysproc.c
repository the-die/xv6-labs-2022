#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;


  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

#ifdef LAB_PGTBL
uint64
sys_pgaccess(void)
{
  static const int max_pages = 64;
  int num;
  argint(1, &num);
  if (num > max_pages)
    return -1;

  uint64 base;
  uint64 mask_addr;
  argaddr(0, &base);
  argaddr(2, &mask_addr);

  pagetable_t pagetable= myproc()->pagetable;
  uint64 mask = 0;
  for (int i = 0; i < num; ++i) {
    pte_t* pte = walk(pagetable, base, 0);
    if (pte == 0)
      return -1;
    if (*pte & PTE_A) {
      *pte &= ~PTE_A;
      mask |= (uint64)1 << i;
    }
    base += PGSIZE;
  }

  if (copyout(pagetable, mask_addr, (char*)&mask, (num + 7) & ~7) < 0)
    return -1;

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
