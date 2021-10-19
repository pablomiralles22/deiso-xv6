#include "kernel/types.h"
#include "kernel/file.h"
#include "kernel/spinlock.h"

#ifndef VMA_H
#define VMA_H

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

#endif /* VMA_H */
