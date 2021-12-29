#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "pstat.h"
#include "vma.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
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
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
sys_sbrk(void)
{
  struct proc *p = myproc();
  struct vma *heap = p->heap,
             *prev_heap;
  int n;
  uint64 addr;

  if(argint(0, &n) < 0)
    return -1;
  if(n < 0 && heap->length < (uint64)(-n))
    return -1;
  // check not overtaking next vma
  if(n > 0) {
    for(prev_heap = &p->vma_start; prev_heap->next != heap; prev_heap = prev_heap->next);
    if(heap->start + heap->length + n > prev_heap->start)
      return -1;
  }

  addr = heap->start + heap->length;
  if(n < 0) vma_free_mem(p->pagetable, heap, heap->start + heap->length + n, -n);
  heap->length += n;
  p->sz = heap->start + heap->length;
  return addr;
}

uint64
sys_sleep(void)
{
int n;
uint ticks0;

if(argint(0, &n) < 0)
  return -1;
acquire(&tickslock);
ticks0 = ticks;
while(ticks - ticks0 < n){
  if(myproc()->killed){
    release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
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

// stablish the number of tickets of the caller process 
uint64
sys_settickets(void)
{
  int n;
  struct proc* p = myproc();

  if(argint(0, &n) < 0 || n < MINTICKETS)
    return -1;
  acquire(&p->lock);
  acquire(&tickets_lock);
  total_tickets += n - p->tickets;
  p->tickets = n;
  release(&tickets_lock);
  release(&p->lock);
  return 0;
}

uint64
sys_getpinfo(void)
{
  uint64 addr;
  struct pstat ps = { 0 };

  if(argaddr(0, &addr) < 0)
    return -1;

  for(int i = 0; i < NPROC; ++i) {
    /* if(proc[i].state != UNUSED) { */
    ps.inuse[i]   = proc[i].state != UNUSED;
    ps.pid[i]     = proc[i].pid;
    ps.tickets[i] = proc[i].tickets;
    ps.ticks[i]   = proc[i].ticks;
  }

  if(copyout(myproc()->pagetable, addr, (char *) &ps, sizeof(struct pstat)) < 0)
    return -1;
  return 0;
}

uint64
sys_mmap(void) {
  struct proc *p = myproc();
  struct file *f;
  uint64 addr;
  size_t length;
  int prot, flags, fd;
  off_t offset;

  if(argaddr(0, &addr) < 0) return -1;
  if(argaddr(1, &length) < 0) return -1;
  if(argint(2, &prot) < 0) return -1;
  if(argint(3, &flags) < 0) return -1;
  if(argint(4, &fd) < 0) return -1;
  if(argaddr(5, &offset) < 0) return -1;

  f = p->ofile[fd];

  if(f == 0) return -1;
  if((prot & PROT_WRITE) && (flags & MAP_SHARED) && (!f->writable))
    return -1;
  if((prot & PROT_READ) && (!f->readable))
    return -1;

  struct vma *vma;
  struct vma *it = &p->vma_start;

  while(it->next != 0) {
    uint64 available_space = 
      PGROUNDDOWN(it->start) - PGROUNDUP(it->next->start + it->next->length);
    if(available_space >= PGROUNDUP(length)) {
      if((vma = vma_alloc()) == 0)
        return -1;
      vma_init(vma, PGROUNDDOWN(it->start) - PGROUNDUP(length),
          length, f, offset, prot, flags, it->next);
      it->next = vma;
      return vma->start;
    }
    it = it->next;
  }
  return -1;
}

uint64
sys_munmap(void) {
  struct proc* p = myproc();
  uint64 addr;
  size_t length;

  if(argaddr(0, &addr) < 0) return -1;
  if(argaddr(1, &length) < 0) return -1;

  struct vma *it = &p->vma_start;
  struct vma *prev = 0;

  for(; it->next != 0; prev = it, it = it->next)
    if(it->start <= addr && it->start + it->length >= addr + length) {
      // found VMA
      if(it->start == addr && it->length == length) {
        prev->next = it->next;
        vma_free(p->pagetable, it);
        return 0;
      }

      if(it->start == addr) { 
        if(PGROUNDDOWN(length) != length) return -1;
        vma_free_mem(p->pagetable, it, addr, length);
        it->start = addr + length;
        it->offset += length;
        it->length -= length;
      } else if(it->start + it->length == addr + length) {
        vma_free_mem(p->pagetable, it, addr, length);
        it->length -= length;
      } else {
        printf("Hole in vma not supported. Range: %p - %p", addr, addr + length);
        return -1;
      }
      return 0;
    }
  printf("VMA not found. Range: %p - %p", addr, addr + length);
  return -1;
}

