// Physical memory allocator, intended to allocate
// memory for user processes, kernel stacks, page table pages,
// and pipe buffers. Allocates 4096-byte pages.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"

#define MAXPAGES (PHYSTOP / PGSIZE)

#define ASSERT_OR_PANIC( exp , msg) \
    ( (exp) ? (void)0 : panic(msg))
	
void freerange(void *vstart, void *vend);
extern char end[]; // first address after kernel loaded from ELF file
                   // defined by the kernel linker script in kernel.ld

struct run {
  uint ref_count;
  struct run *next;
};

//helper functions in this file
void 			zeroRefCountRun(struct run*);
void 			inclRefCountRun(struct run*);
void 			declRefCountRun(struct run*);
struct {
  struct spinlock lock;
  int use_lock;
  struct run *freelist;
  // Lab 2: For COW fork, we can't store the run in the
  // physical page, because we need space for the ref
  // count.  Moving the data to the kmem struct.
  struct run runs[MAXPAGES];
} kmem;

// Initialization happens in two phases.
// 1. main() calls kinit1() while still using entrypgdir to place just
// the pages mapped by entrypgdir on free list.
// 2. main() calls kinit2() with the rest of the physical pages
// after installing a full page table that maps them on all cores.
void
kinit1(void *vstart, void *vend)
{
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  freerange(vstart, vend);
}

void
kinit2(void *vstart, void *vend)
{
  freerange(vstart, vend);
  kmem.use_lock = 1;
}

void
freerange(void *vstart, void *vend)
{
  char *p;
  p = (char*)PGROUNDUP((uint)vstart);
  for(; p + PGSIZE <= (char*)vend; p += PGSIZE)
    kfree2(p);
  
}
// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)

//Just for freerange, dont assert ref_count
void
kfree2(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree2");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);

  // Lab2: because we moved 'runs' to kmem
  //r = (struct run*)v;
  r = &kmem.runs[(V2P(v) / PGSIZE)];
  r->next = kmem.freelist;
  kmem.freelist = r;
  zeroRefCountRun(r);
  if(kmem.use_lock)
    release(&kmem.lock);
}

void
kfree(char *v)
{
  struct run *r;

  if((uint)v % PGSIZE || v < end || V2P(v) >= PHYSTOP)
    panic("kfree1");

  // Fill with junk to catch dangling refs.
  memset(v, 1, PGSIZE);

  if(kmem.use_lock)
    acquire(&kmem.lock);

  // Lab2: because we moved 'runs' to kmem
  //r = (struct run*)v;

  r = &kmem.runs[(V2P(v) / PGSIZE)];
  //assert ref count to 1, reset it to 0
  ASSERT_OR_PANIC(r->ref_count==1,"kfree2 ref_count!=1");
  r->next = kmem.freelist;
  kmem.freelist = r;
  zeroRefCountRun(r);
  if(kmem.use_lock)
	release(&kmem.lock);

}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
char*
kalloc(void)
{
  struct run *r;
  char *rv;

  if(kmem.use_lock)
    acquire(&kmem.lock);
  r = kmem.freelist;
  if(r){
    kmem.freelist = r->next;
	//set ref count at 1
	zeroRefCountRun(r);
	inclRefCountRun(r);
  }
  if(kmem.use_lock)
    release(&kmem.lock);

  // Lab2: because we moved 'runs' to kmem
  //return (char*)r;
  rv = r ? P2V((r - kmem.runs) * PGSIZE) : r;
  return rv;
}

//atomic helper functions
void
zeroRefCountRun(struct run* runp){
	__sync_fetch_and_and(&(runp->ref_count),0);
}

void
inclRefCountRun(struct run* runp){
	__sync_fetch_and_add(&(runp->ref_count),1);
}

void
declRefCountRun(struct run* runp){
	__sync_fetch_and_sub(&(runp->ref_count),1);
}


//helper to be used outside
void
inclRefCount(char *v){
  struct run *r;
  if(kmem.use_lock)
	acquire(&kmem.lock);
  r = &kmem.runs[(V2P(v) / PGSIZE)];
  __sync_fetch_and_add(&(r->ref_count),1);
  if(kmem.use_lock)
    release(&kmem.lock);
	
}
void
declRefCount(char *v){
  struct run *r;
  if(kmem.use_lock)
	acquire(&kmem.lock);
  r = &kmem.runs[(V2P(v) / PGSIZE)];
  __sync_fetch_and_sub(&(r->ref_count),1);
  if(kmem.use_lock)
    release(&kmem.lock);
}

uint
getRefCount(char *v){
  struct run *r;
  if(kmem.use_lock)
	acquire(&kmem.lock);
  r = &kmem.runs[(V2P(v) / PGSIZE)];
  if(kmem.use_lock)
    release(&kmem.lock);
  return r->ref_count;
}


