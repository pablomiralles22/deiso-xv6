#include "file.h"
#include "defs.h"
#include "vma.h"
#include "proc.h"

struct vma vma_list[NVMA];

struct vma *vma_alloc() {
  struct vma* vma;
  for(vma = vma_list; vma < &vma_list[NVMA]; vma++) {
    acquire(&vma->lock);
    if(vma->length == 0) return vma;
    release(&vma->lock);
  }
  panic("Out of VMAs");
}

void vma_free_mem(struct vma *vma, struct vma *prev, uint64 addr, uint64 length) {
  struct file *f = vma->file;
  struct proc *p = myproc();
  int write = (vma->flags & MAP_SHARED);
  uint64 pos;
  uint64 file_pos;
  uint64 start;
  uint64 end;
  int free_start, free_end;

  free_start = (!prev || 
                (PGROUNDDOWN(prev->start + prev->length) != prev->start + prev->length &&
                 PGROUNDDOWN(prev->start + prev->length) != PGROUNDDOWN(vma->start)));

  free_end = (!vma->next || 
              (PGROUNDDOWN(vma->start + vma->length) != vma->start + vma->length &&
                PGROUNDDOWN(vma->start + vma->length) != PGROUNDDOWN(vma->next->start)));


  for(pos = PGROUNDDOWN(addr); pos < addr + length; pos += PGSIZE) {
    start = addr > pos ? addr : pos;
    end = pos + PGSIZE > addr + length ? addr + length : pos + PGSIZE;
    file_pos = start - vma->start + vma->offset;

    if(is_page_mapped(p->pagetable, pos)) {
      if(write) {
        ilock(f->ip);
        writei(f->ip, 1, pos, file_pos, end - start);
        iunlock(f->ip);
      }
      if((pos == PGROUNDDOWN(addr)          && free_start) || 
         (pos == PGROUNDDOWN(addr + length) && free_end  ) ||
          end - start == PGSIZE)
        uvmunmap(p->pagetable, pos, 1, 1);
    }
  }
}

void vma_free(struct vma *vma, struct vma *prev) {
  acquire(&vma->lock);
  if(vma->file != 0) {
    vma_free_mem(vma, prev, vma->start, vma->length);
    fileclose(vma->file);
  }
  vma->start = 0;
  vma->length = 0;
  vma->next = 0;
  vma->file = 0;
  vma->offset = 0;
  vma->permission = 0;
  vma->flags = 0;
  release(&vma->lock);
}

// needs a's lock to be taken
void vma_copy(struct vma *a, struct vma *b) {
  a->start = b->start;
  a->length = b->length;
  a->next = b->next;
  a->file = b->file;
  a->offset = b->offset;
  a->permission = b->permission;
  a->flags = b->flags;
}
