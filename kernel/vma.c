#include "file.h"
#include "defs.h"
#include "vma.h"
#define NVMA 128

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

void vma_free(struct vma *vma) {
  acquire(&vma->lock);
  fileclose(vma->file); // TODO: if we have vmas that don't correspond to file, check
  vma->start = 0;
  vma->length = 0;
  vma->length = 0;
  vma->length = 0;
  vma->length = 0;
  release(&vma->lock);
}
