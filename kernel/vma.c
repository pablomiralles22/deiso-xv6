#include "file.h"
#include "defs.h"
#include "vma.h"
#include "proc.h"

struct vma vma_list[NVMA];

struct vma *vma_alloc() {
  struct vma* vma;
  for(vma = vma_list; vma < &vma_list[NVMA]; vma++) {
    acquire(&vma->lock);
    if(vma->used == 0) return vma;
    release(&vma->lock);
  }
  return 0;
  panic("Out of VMAs");
}

void vma_free_mem(pagetable_t pagetable, struct vma *vma, uint64 addr, uint64 length) {
  struct file *f = vma->file;
  int write = (vma->flags & MAP_SHARED) && vma->file;
  uint64 pos, file_pos, start, end, write_end;

  for(pos = PGROUNDDOWN(addr); pos < addr + length; pos += PGSIZE) {
    start = addr > pos ? addr : pos;
    end = pos + PGSIZE > addr + length ? addr + length : pos + PGSIZE;
    write_end = end > vma->start + vma->file_length
                ? vma->start + vma->file_length : end;
    file_pos = start - vma->start + vma->offset;

    if(is_page_mapped(pagetable, pos)) {
      if(write && write_end > start) {
        begin_op();
        ilock(f->ip);
        writei(f->ip, 1, start, file_pos, write_end - start);
        iunlock(f->ip);
        end_op();
      }
      if((start > pos && start > vma->start) ||
         (end < pos+PGSIZE && end < vma->start + vma->length)) {
        // fill with junk, since we will not unmap
        uint64 pa = walkaddr(pagetable, start);
        memset((void *)(pa + (start - pos)), 0, end - start);
        // do not unmap if there is still part of the vma in the page
        continue;
      }
      uvmunmap(pagetable, pos, 1, 1);
    }
  }
}

void vma_free(pagetable_t pagetable, struct vma *vma) {
  vma_free_mem(pagetable, vma, vma->start, vma->length);
  if(vma->file != 0)
    fileclose(vma->file);
  vma->start = 0;
  vma->next = 0;
  vma->file = 0;
  vma->offset = 0;
  vma->permission = 0;
  vma->flags = 0;
  vma->length = 0;
  vma->file_length = 0;

  acquire(&vma->lock);
  vma->used = 0;
  release(&vma->lock);
}

// needs a's lock to be taken
void vma_copy(struct vma *a, struct vma *b) {
  a->used = b->used;
  a->start = b->start;
  a->length = b->length;
  a->file_length = b->file_length;
  a->next = b->next;
  a->file = b->file;
  a->offset = b->offset;
  a->permission = b->permission;
  a->flags = b->flags;
}

// call with vma->lock acquired
void vma_init(struct vma *vma, uint64 start, uint64 length,
              struct file *file, uint64 offset, int permission,
              int flags, struct vma *next)
{
  _vma_init(vma, start, length, length, file, offset, permission, flags, next);
}

void _vma_init(struct vma *vma, uint64 start, uint64 length,
              uint64 file_length, struct file *file, uint64 offset,
              int permission, int flags, struct vma *next) {

  vma->used = 1;
  release(&vma->lock);
  if(file) filedup(file);
  vma->length = length;
  vma->start = start;
  vma->length = length;
  vma->file_length = file_length;
  vma->file = file;
  vma->offset = offset;
  vma->permission = permission;
  vma->flags = flags;
  vma->next = next;
}

