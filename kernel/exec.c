#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "vma.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  struct file *elf_file;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();
  struct vma *vma = 0,
             *aux = 0,
             *old_vma = p->vma_start.next; // needed to restore in case of error

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  // Check ELF header
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Get file for VMAs
  if((elf_file = filealloc()) < 0)
    goto bad;
  elf_file->type     = FD_INODE;
  elf_file->off      = 0;
  elf_file->readable = 1;
  elf_file->writable = 0;
  elf_file->ref      = 1; // avoid filedup panic
  elf_file->ip       = ip;


  // Load program into memory.
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    sz = ph.vaddr + ph.memsz;
    if((ph.vaddr % PGSIZE) != 0)
      goto bad;
    // Get VMA for segment
    int permission = 0;
    if(ph.flags & ELF_PROG_FLAG_WRITE)
      permission |= PROT_WRITE;
    if(ph.flags & ELF_PROG_FLAG_READ)
      permission |= PROT_READ;
    if(ph.flags & ELF_PROG_FLAG_EXEC)
      permission |= PROT_EXEC;
    aux = vma;
    if((vma = vma_alloc()) == 0)
      goto bad;
    _vma_init(vma, ph.vaddr, ph.memsz, ph.filesz, elf_file,
        ph.off, permission, MAP_PRIVATE, aux);
  }
  if(elf_file->ref <= 1) goto bad;
  else elf_file->ref--; // shouldn't have any problem with concurrency
  iunlock(ip); // don't deref inode on finish
  end_op();
  ip = 0;

  // Allocate VMA for stack
  aux = vma;
  if((vma = vma_alloc()) == 0)
    goto bad;
  sz = PGROUNDUP(sz);
  sz += 2 * PGSIZE;
  sp = sz;
  stackbase = sp - PGSIZE;
  _vma_init(vma, stackbase, PGSIZE, 0, 0, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE, aux);

  // Allocate VMA for heap
  aux = vma;
  if((vma = vma_alloc()) == 0)
    goto bad;
  _vma_init(vma, sz, 0, 0, 0, 0, PROT_READ | PROT_WRITE, MAP_PRIVATE, aux);

  // Need new vmas before writing to stack
  p->vma_start.next = vma;
  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  /** printf("EXEC CALL from PID=%d\n", p->pid); */
  /** for(struct vma *it = &p->vma_start; it != 0; it = it->next) { */
  /**   printf("VMA %p, [%p, %p]\n", it, it->start, it->start+it->length); */
  /** } */
  /** printf("----------\n"); */

  // push the array of argv[] pointers.
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = main
  p->trapframe->sp = sp; // initial stack pointer
  for(struct vma *it = old_vma, *next; it != 0; it = next) {
    next = it->next;
    vma_free(oldpagetable, it);
  }
  proc_freepagetable(oldpagetable, 0);
  p->heap = vma; // set heap VMA

  // Stop lazy alloc por binary
  /** struct vma *it; */
  /** for(it = vma; it->next != 0; it = it->next); */
  /** allocrange(pagetable, vma->start, vma->length); */

  return argc; // this ends up in a0, the first argument to main(argc, argv)

 bad:
  p->vma_start.next = old_vma; // undo change of VMAs
  if(pagetable) {
    for(struct vma *it = vma, *next; it != 0; it = next) {
      next = it->next;
      vma_free(pagetable, it);
    }
    proc_freepagetable(pagetable, 0);
  }
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}


