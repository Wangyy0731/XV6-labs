// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

struct run *steal(int cpu_id);

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  struct run *freelist;
  char lock_name[8];//每个CPU对应的锁的名称
} kmem[NCPU];

void
kinit()
{
  for(int i=0;i<NCPU;i++)
  {
    snprintf(kmem[i].lock_name,8,"kmem_%d",i);
    initlock(&kmem[i].lock, kmem[i].lock_name);
  }
  freerange(end, (void*)PHYSTOP);
}

void
freerange(void *pa_start, void *pa_end)
{
  char *p;
  p = (char*)PGROUNDUP((uint64)pa_start);
  for(; p + PGSIZE <= (char*)pa_end; p += PGSIZE)
    kfree(p);
}

// Free the page of physical memory pointed at by v,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void
kfree(void *pa)
{
  struct run *r;
  int number_cpu;

  if(((uint64)pa % PGSIZE) != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");

  // Fill with junk to catch dangling refs.
  memset(pa, 1, PGSIZE);

  r = (struct run*)pa;

  //得到当前的CPU号
  push_off();
  number_cpu=cpuid();
  pop_off();

  //释放对应的物理页
  acquire(&kmem[number_cpu].lock);
  r->next = kmem[number_cpu].freelist;
  kmem[number_cpu].freelist = r;
  release(&kmem[number_cpu].lock);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *
kalloc(void)
{
  struct run *r;
  int number_cpu;

  push_off();
  number_cpu=cpuid();
  pop_off();

  acquire(&kmem[number_cpu].lock);
  r = kmem[number_cpu].freelist;
  if(r)
    kmem[number_cpu].freelist = r->next;
  release(&kmem[number_cpu].lock);

  if(!r&&(r=steal(number_cpu)))//没有系统空间时窃取空间
  {
    acquire(&kmem[number_cpu].lock);
    kmem[number_cpu].freelist=r->next;
    release(&kmem[number_cpu].lock);
  }

  if(r)
    memset((char*)r, 5, PGSIZE); // fill with junk

  
  return (void*)r;
}

struct run *steal(int cpu_id)
{
  int j=cpu_id;
  struct run *fast, *slow, *head;

  //说明此时在kalloc的时候，发生了进程切换
  if(cpu_id!=cpuid())
  {
    panic("steal");
  }

  for(int i=0;i<NCPU;i++)
  {
    if(++j==NCPU)//循环遍历整个CPU数组
    {
      j=0;
    }

     acquire(&kmem[j].lock);
        // 若链表不为空
        if (kmem[j].freelist) {
            // 快慢双指针算法将链表一分为二
            slow = head = kmem[j].freelist;
            fast = slow->next;
            while (fast) {
                fast = fast->next;
                if (fast) {
                    slow = slow->next;
                    fast = fast->next;
                }
            }
            // 后半部分作为当前CPU的空闲链表
            kmem[j].freelist = slow->next;
            release(&kmem[j].lock);
            // 前半部分的链表结尾清空,由于该部分链表与其他链表不再关联,因此无需加锁
            slow->next = 0;
            // 返回前半部分的链表头
            return head;
        }
        release(&kmem[j].lock);
  }
  return 0; //此时所有CPU均无空闲空间，无法steal
}