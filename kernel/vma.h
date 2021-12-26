#ifndef VMA_H
#define VMA_H

#include "types.h"
#include "file.h"
#include "spinlock.h"
#include "param.h"

#define PROT_READ           0x001
#define PROT_WRITE          0x002

#define MAP_SHARED          0x001
#define MAP_PRIVATE         0x002

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
void vma_free_mem(struct vma *vma, uint64 addr, uint64 length);
void vma_free(struct vma *vma);
void vma_copy(struct vma *a, struct vma *b);

#endif /* VMA_H */
