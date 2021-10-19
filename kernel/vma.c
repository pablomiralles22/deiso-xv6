#include "kernel/vma.h"
#define NVMA 128

struct vma vma_list[NVMA];

struct vma *vma_alloc() {
  struct vma* vma;
  for(vma = vma_list; vma < vma_list[NVMA]; vma++) {
    acquire(&vma->lock);
    if(vma->size == 0) return vma;
    release(&vma->lock);
  }
  panic("Out of VMAs");
}
