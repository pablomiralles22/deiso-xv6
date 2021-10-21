#ifndef VMA_H
#define VMA_H

#include "types.h"
#include "file.h"
#include "spinlock.h"
#include "param.h"

struct vma {
  uint64 start;
  uint64 length;
  struct file *file;
  uint64 offset;
  int permission;
  int flags;
  struct vma *next;

  struct spinlock lock;
};

struct vma *vma_alloc();
void vma_free(struct vma *vma);
void vma_copy(struct vma *a, struct vma *b);

#endif /* VMA_H */
