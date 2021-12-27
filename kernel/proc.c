#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "vma.h"
#include "proc.h"
#include "defs.h"

static uint ticks_last_start;

uint total_tickets = 0;
struct spinlock tickets_lock;

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->ticks = 0;

  p->vma_end.length = 0;
  p->vma_start.next = &p->vma_end;

  acquire(&tickets_lock);
  if(p->state == RUNNING || p->state == RUNNABLE)
    total_tickets -= p->tickets;
  p->tickets = 0;
  p->state = UNUSED;
  release(&tickets_lock);
}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// od -t xC initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  p->ticks = 0;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->tickets = MINTICKETS;

  acquire(&tickets_lock);
  total_tickets += p->tickets;
  p->state = RUNNABLE;
  release(&tickets_lock);

  p->vma_start.file = 0;
  p->vma_start.length = 2 * PGSIZE;
  p->vma_start.start = MAXVA - 2 * PGSIZE;
  p->vma_start.permission = 0;
  p->vma_start.offset = 0;
  p->vma_start.next = &p->vma_end;

  p->vma_end.file = 0;
  p->vma_end.length = PGSIZE;
  p->vma_end.start = 0;
  p->vma_end.permission = 0;
  p->vma_end.offset = 0;
  p->vma_end.next = 0;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // Set number of tickets to minimum and ticks to 0
  np->tickets = p->tickets;
  np->ticks = 0;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);
  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  acquire(&tickets_lock);
  total_tickets += np->tickets;
  np->state = RUNNABLE;
  release(&tickets_lock);

  vma_copy(&np->vma_start, &p->vma_start);
  vma_copy(&np->vma_end, &p->vma_end);

  struct vma *it1 = &p->vma_start,
             *it2 = &np->vma_start;
  /** struct vma *it2 = &np->vma_start; */

  while(it1->next != &p->vma_end) {
    it2->next = vma_alloc();
    it1 = it1->next;
    it2 = it2->next;
    vma_copy(it2, it1);
    release(&it2->lock);
    filedup(it2->file);
    if(uvmcopy_offseted(p->pagetable, np->pagetable, it1->start, it1->length) < 0){
      for(struct vma *it = &np->vma_start; it != it2; it = it->next)
        vma_free(it);
      it2->file = 0;
      vma_free(it2);
      freeproc(np);
      release(&np->lock);
      return -1;
    }
  }
  it2->next = &np->vma_end; // no need for lock
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  for(struct vma *it = p->vma_start.next, *next; (next = it->next) != 0; it = next)
    vma_free(it);

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  acquire(&tickets_lock);
  if(p->state == RUNNING || p->state == RUNNABLE)
    total_tickets -= p->tickets;
  p->state = ZOMBIE;
  release(&tickets_lock);

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
        freeproc(np);
        release(&np->lock);
        release(&wait_lock);
        return pid;
      }
      release(&np->lock);
    }
  }

  // No point waiting if we don't have any children.
  if(!havekids || p->killed){
    release(&wait_lock);
    return -1;
  }
  
  // Wait for a child to exit.
  sleep(p, &wait_lock);  //DOC: wait-sleep
}
}

/*
* RANDOM NUMBER GENERATOR
* https://en.wikipedia.org/wiki/Lehmer_random_number_generator
* https://www.cs.virginia.edu/~cr4bd/4414/F2019/files/lcg_parkmiller_c.txt
*/
static unsigned random_seed = 1;

#define RANDOM_MAX ((1u << 63u) - 1u)
unsigned lcg_parkmiller(unsigned *state)
{
const unsigned N = 0x7fffffff;
const unsigned G = 48271u;

unsigned div = *state / (N / G);  /* max : 2,147,483,646 / 44,488 = 48,271 */
unsigned rem = *state % (N / G);  /* max : 2,147,483,646 % 44,488 = 44,487 */

unsigned a = rem * G;        /* max : 44,487 * 48,271 = 2,147,431,977 */
unsigned b = div * (N % G);  /* max : 48,271 * 3,399 = 164,073,129 */

return *state = (a > b) ? (a - b) : (a + (N - b));
}

unsigned next_random() {
return lcg_parkmiller(&random_seed);
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p, *selected;
  struct cpu *c = mycpu();
  int cum_tickets, target_tickets; // TODO long long?

  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    cum_tickets = 0;
    selected = 0;

    acquire(&tickets_lock);
    if(total_tickets == 0) {
      release(&tickets_lock);
      continue;
    }
    target_tickets = (next_random() % total_tickets) + MINTICKETS;

    for(p = proc; p < &proc[NPROC]; p++)
      if(p->state == RUNNABLE) {
        cum_tickets += p->tickets;
        selected = p;
        if(cum_tickets >= target_tickets)
          break;
      }

    if(selected != 0) {
      acquire(&selected->lock);
      release(&tickets_lock);
      /** printf("CPU assigned to %d\n", selected->pid); */
      selected->state = RUNNING;
      c->proc = selected;
      swtch(&c->context, &selected->context);
      c->proc = 0;
      release(&selected->lock);
      // Read ticks and write in global variable
      acquire(&tickslock);
      ticks_last_start = ticks;
      release(&tickslock);
    } else release(&tickets_lock);
  }
}


// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
int intena;
struct proc *p = myproc();

if(!holding(&p->lock))
  panic("sched p->lock");
if(mycpu()->noff != 1)
  panic("sched locks");
if(p->state == RUNNING)
  panic("sched running");
if(intr_get())
  panic("sched interruptible");

// Add difference of ticks to process' ticks counter
acquire(&tickslock);
p->ticks += ticks - ticks_last_start;
release(&tickslock);

intena = mycpu()->intena;
swtch(&p->context, &mycpu()->context);


mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  acquire(&tickets_lock);
  if(p->state != RUNNABLE && p->state != RUNNING)
    total_tickets += p->tickets;
  p->state = RUNNABLE;
  release(&tickets_lock);
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
static int first = 1;

// Still holding p->lock from scheduler.
release(&myproc()->lock);

if (first) {
  // File system initialization must be run in the context of a
  // regular process (e.g., because it calls sleep), and thus cannot
  // be run from main().
  first = 0;
  fsinit(ROOTDEV);
}

usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  acquire(&tickets_lock);
  if(p->state == RUNNING || p->state == RUNNABLE)
    total_tickets -= p->tickets;
  p->state = SLEEPING;
  release(&tickets_lock);

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
struct proc *p;

for(p = proc; p < &proc[NPROC]; p++) {
  if(p != myproc()){
    acquire(&p->lock);
    if(p->state == SLEEPING && p->chan == chan) {
      acquire(&tickets_lock);
      p->state = RUNNABLE;
      total_tickets += p->tickets;
      release(&tickets_lock);
    }
    release(&p->lock);
  }
}
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
struct proc *p;

for(p = proc; p < &proc[NPROC]; p++){
  acquire(&p->lock);
  if(p->pid == pid){
    p->killed = 1;
    if(p->state == SLEEPING){
      // Wake process from sleep().
      acquire(&tickets_lock);
      p->state = RUNNABLE;
      total_tickets += p->tickets;
      release(&tickets_lock);
    }
    release(&p->lock);
    return 0;
  }
  release(&p->lock);
}
return -1;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
static char *states[] = {
[UNUSED]    "unused",
[SLEEPING]  "sleep ",
[RUNNABLE]  "runble",
[RUNNING]   "run   ",
[ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
