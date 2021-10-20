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

#endif /* VMA_H */
