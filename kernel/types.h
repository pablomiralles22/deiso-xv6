#ifndef TYPES_H
#define TYPES_H
typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;

typedef unsigned char uint8;
typedef unsigned short uint16;
typedef unsigned int  uint32;
typedef unsigned long uint64;

typedef uint64 pde_t;

# ifndef __size_t_defined
typedef uint64 size_t;
# endif

# ifndef __off_t_defined
typedef uint64 off_t; // TODO: should be signed?
# endif
#endif /* TYPES_H */
