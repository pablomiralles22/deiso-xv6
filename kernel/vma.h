#ifndef VMA_H
#define VMA_H

#include "defs.h"
#include "types.h"
#include "file.h"
#include "spinlock.h"
#include "param.h"
#include "vma_flags.h"

struct vma {
  int used;
  uint64 start;
  uint64 length;
  uint64 file_length;
  struct file *file;
  uint64 offset;
  int permission;
  int flags;
  struct vma *next;

  struct spinlock lock;
};

struct vma *vma_alloc();
void vma_free_mem(pagetable_t pagetable, struct vma *vma, uint64 addr, uint64 length);
void vma_free(pagetable_t pagetable, struct vma *vma);
void vma_copy(struct vma *a, struct vma *b);
void vma_init(struct vma *vma, uint64 start, uint64 length,
              struct file *file, uint64 offset, int permission,
              int flags, struct vma *next);
void _vma_init(struct vma *vma, uint64 start, uint64 length,
               uint64 file_length, struct file *file, uint64 offset,
               int permission, int flags, struct vma *next);

#endif /* VMA_H */
