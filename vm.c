#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "elf.h"
#include "vdso.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void seginit(void) {
	struct cpu *c;

	// Map "logical" addresses to virtual addresses using identity map.
	// Cannot share a CODE descriptor for both kernel and user
	// because it would have to have DPL_USR, but the CPU forbids
	// an interrupt from CPL=0 to DPL=3.
	c = &cpus[cpuid()];
	c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
	c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
	c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
	c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
	lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc) {
	pde_t *pde;
	pte_t *pgtab;

	pde = &pgdir[PDX(va)];
	if (*pde & PTE_P) {
		pgtab = (pte_t*) P2V(PTE_ADDR(*pde));
	} else {
		if (!alloc || (pgtab = (pte_t*) kalloc()) == 0)
			return 0;
		// Make sure all those PTE_P bits are zero.
		memset(pgtab, 0, PGSIZE);
		// The permissions here are overly generous, but they can
		// be further restricted by the permissions in the page table
		// entries, if necessary.
		*pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
	}
	return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm) {
	char *a, *last;
	pte_t *pte;

	a = (char*) PGROUNDDOWN((uint )va);
	last = (char*) PGROUNDDOWN(((uint )va) + size - 1);
	for (;;) {
		if ((pte = walkpgdir(pgdir, a, 1)) == 0)
			return -1;
		if (*pte & PTE_P)
			panic("remap");
		*pte = pa | perm | PTE_P;
		if (a == last)
			break;
		a += PGSIZE;
		pa += PGSIZE;
	}
	return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
	void *virt;
	uint phys_start;
	uint phys_end;
	int perm;
} kmap[] = { { (void*) KERNBASE, 0, EXTMEM, PTE_W }, // I/O space
		{ (void*) KERNLINK, V2P(KERNLINK), V2P(data), 0 },   // kern text+rodata
		{ (void*) data, V2P(data), PHYSTOP, PTE_W }, // kern data+memory
		{ (void*) DEVSPACE, DEVSPACE, 0, PTE_W }, // more devices
		};

// Set up kernel part of a page table.
pde_t*
setupkvm(void) {
	pde_t *pgdir;
	struct kmap *k;

	if ((pgdir = (pde_t*) kalloc()) == 0)
		return 0;
	memset(pgdir, 0, PGSIZE);
	if (P2V(PHYSTOP) > (void*) DEVSPACE)
		panic("PHYSTOP too high");
	for (k = kmap; k < &kmap[NELEM(kmap)]; k++)
		if (mappages(pgdir, k->virt, k->phys_end - k->phys_start,
				(uint) k->phys_start, k->perm) < 0) {
			freevm(pgdir, (struct proc *) 0);
			return 0;
		}
	return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void kvmalloc(void) {
	kpgdir = setupkvm();
	switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void switchkvm(void) {
	lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void switchuvm(struct proc *p) {
	if (p == 0)
		panic("switchuvm: no process");
	if (p->kstack == 0)
		panic("switchuvm: no kstack");
	if (p->pgdir == 0)
		panic("switchuvm: no pgdir");

	pushcli();
	mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
			sizeof(mycpu()->ts) - 1, 0);
	mycpu()->gdt[SEG_TSS].s = 0;
	mycpu()->ts.ss0 = SEG_KDATA << 3;
	mycpu()->ts.esp0 = (uint) p->kstack + KSTACKSIZE;
	// setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
	// forbids I/O instructions (e.g., inb and outb) from user space
	mycpu()->ts.iomb = (ushort) 0xFFFF;
	ltr(SEG_TSS << 3);
	lcr3(V2P(p->pgdir));  // switch to process's address space
	popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void inituvm(pde_t *pgdir, char *init, uint sz) {
	char *mem;

	if (sz >= PGSIZE)
		panic("inituvm: more than a page");
	mem = kalloc();
	memset(mem, 0, PGSIZE);
	mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W | PTE_U);
	memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz) {
	uint i, pa, n;
	pte_t *pte;

	if ((uint) addr % PGSIZE != 0)
		panic("loaduvm: addr must be page aligned");
	for (i = 0; i < sz; i += PGSIZE) {
		if ((pte = walkpgdir(pgdir, addr + i, 0)) == 0)
			panic("loaduvm: address should exist");
		pa = PTE_ADDR(*pte);
		if (sz - i < PGSIZE)
			n = sz - i;
		else
			n = PGSIZE;
		if (readi(ip, P2V(pa), offset + i, n) != n)
			return -1;
	}
	return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int allocuvm(pde_t *pgdir, uint oldsz, uint newsz, struct proc * currproc) {
	char *mem;
	uint a;

	if (newsz >= KERNBASE)
		return 0;
	if (newsz < oldsz)
		return oldsz;

	a = PGROUNDUP(oldsz);
	for (; a < newsz; a += PGSIZE) {
		mem = kalloc();
		if (mem == 0) {
			cprintf("allocuvm out of memory\n");
			deallocuvm(pgdir, newsz, oldsz, currproc);
			return 0;
		}
		memset(mem, 0, PGSIZE);
		if (mappages(pgdir, (char*) a, PGSIZE, V2P(mem), PTE_W | PTE_U) < 0) {
			cprintf("allocuvm out of memory (2)\n");
			deallocuvm(pgdir, newsz, oldsz, currproc);
			kfree(mem);
			return 0;
		}
	}
	return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int deallocuvm(pde_t *pgdir, uint oldsz, uint newsz, struct proc *currproc) {
	pte_t *pte;
	uint a, pa;

	if (newsz >= oldsz)
		return oldsz;

	a = PGROUNDUP(newsz);

	for (; a < oldsz; a += PGSIZE) {
		pte = walkpgdir(pgdir, (char*) a, 0);
		//guard pg
		if (currproc != 0 && a == currproc->guardpg) {
			*pte = *pte | PTE_P;
		}

		if (!pte)
			a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
		else if ((*pte & PTE_P) != 0) {
			pa = PTE_ADDR(*pte);
			if (pa == 0) {
				cprintf("a: %x, oldsz: %x, newsz: %x\n", a, oldsz, newsz);
				cprintf(
						"PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
						*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
						*pte & PTE_U, currproc->sz, currproc->guardpg,
						currproc->stacktop, currproc->stacklastpg);
				panic("kfree");
			}
			char *v = P2V(pa);
			//if ref count>1 just decl by 1
			if (getRefCount(v) == 1) {
				kfree(v);
			} else {
				declRefCount(v);
			}
			*pte = 0;
		}
	}
	return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void freevm(pde_t *pgdir, struct proc *currproc) {
	uint i;

	if (pgdir == 0)
		panic("freevm: no pgdir");
	deallocuvm(pgdir, KERNBASE, 0, currproc);
	for (i = 0; i < NPDENTRIES; i++) {
		if (pgdir[i] & PTE_P) {
			char * v = P2V(PTE_ADDR(pgdir[i]));
			if (getRefCount(v) == 1) //free page if only ref count=1
				kfree(v);
			else
				declRefCount(v);
		}
	}
	kfree((char*) pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void clearpteu(pde_t *pgdir, char *uva) {
	pte_t *pte;

	pte = walkpgdir(pgdir, uva, 0);
	if (pte == 0)
		panic("clearpteu");
	*pte &= ~PTE_U;
}

// Clear PTE_P on a page. Used to create an inaccessible
// page beneath the user stack.
void clearptep(pde_t *pgdir, char *uva) {
	pte_t *pte;

	pte = walkpgdir(pgdir, uva, 0);
	if (pte == 0)
		panic("clearptep");
	*pte &= ~PTE_P;
}

// mark PTE_P and PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void markpteup(pde_t *pgdir, char *uva) {
	pte_t *pte;
	pte = walkpgdir(pgdir, uva, 0);
	if (pte == 0)
		panic("markpteup");
	*pte = *pte | PTE_P;
	*pte = *pte | PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz) {
	pde_t *d;
	pte_t *pte;
	uint pa, i, flags;
	char *mem;

	if ((d = setupkvm()) == 0)
		return 0;
	for (i = 0; i < sz; i += PGSIZE) {
		if ((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
			panic("copyuvm: pte should exist");
		if (!(*pte & PTE_P))
			panic("copyuvm: page not present");
		pa = PTE_ADDR(*pte);
		flags = PTE_FLAGS(*pte);
		if ((mem = kalloc()) == 0)
			goto bad;
		memmove(mem, (char*) P2V(pa), PGSIZE);
		if (mappages(d, (void*) i, PGSIZE, V2P(mem), flags) < 0)
			goto bad;
	}
	return d;

	bad: freevm(d, (struct proc *) 0);
	return 0;
}

//cowuvm lab2
pde_t*
cowuvm(pde_t *pgdir, uint sz, struct proc *curproc) {
	pde_t *d;
	pte_t *pte;
	uint pa, i, flags;
	//char *mem;

	if ((d = setupkvm()) == 0)
		return 0;
	//dont copy first page, null page
	for (i = PGSIZE; i < sz; i += PGSIZE) {
		//inside shm
		if (i >= curproc->stacktop && i < curproc->shmtop) {
			if (i >= curproc->shmnextpg) {
				continue;
			} else {
				//a shm page
				if ((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
					panic("cowuvm: pte should exist");

				pa = PTE_ADDR(*pte);
				flags = PTE_FLAGS(*pte);

				//map to the same page pa
				if (mappages(d, (void*) i, PGSIZE, pa, flags) < 0)
					goto bad;

				//increase the ref count
				char *v = P2V(pa);
				inclRefCount(v);

				//shootdown tlb
				invlpg((void *) i);
				continue;
			}
		}

		//inside stack
		if (i >= curproc->stacklastpg && i < curproc->stacktop) {
			if (i < curproc->guardpg) {
				continue;
			} else {
//			if ((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
//				panic("copyuvm: pte should exist");
//			pa = PTE_ADDR(*pte);
//			flags = PTE_FLAGS(*pte);
//			if ((mem = kalloc()) == 0)
//				goto bad;
//			memmove(mem, (char*) P2V(pa), PGSIZE);
//			if (mappages(d, (void*) i, PGSIZE, V2P(mem), flags) < 0)
//				goto bad;

				if ((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
					panic("cowuvm: pte should exist");

				*pte = *pte | PTE_COW;
				*pte = *pte & ~PTE_W;
				//*pte = *pte | PTE_U;

				pa = PTE_ADDR(*pte);
				flags = PTE_FLAGS(*pte);

				//map to the same page pa
				if (mappages(d, (void*) i, PGSIZE, pa, flags) < 0)
					goto bad;

				//increase the ref count
				char *v = P2V(pa);
				inclRefCount(v);

				//shootdown tlb
				invlpg((void *) i);

				if (i == curproc->guardpg) {
					clearpteu(d, (char*) (curproc->guardpg));
					clearptep(d, (char*) (curproc->guardpg));
				}
				continue;
			}
		}

		//not shm or stack
		if ((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
			panic("cowuvm: pte should exist");
		if (!(*pte & PTE_P)) {
			panic("cowuvm: page not present");

		}
		//marking as copy on write via PTE_COW
		//if page is already read only, dont cow
		//if writeable mark as read only and PTE_COW
		if (*pte & PTE_W) {
			*pte = *pte | PTE_COW;
			//remove write bit, marking read only
			*pte = *pte & ~PTE_W;
		}
		//*pte = *pte | PTE_U;
		pa = PTE_ADDR(*pte);
		flags = PTE_FLAGS(*pte);
		//No need to alloc a page. Not copying.
		//if((mem = kalloc()) == 0)
		//  goto bad;
		//memmove(mem, (char*)P2V(pa), PGSIZE);

		//map to the same page pa
		if (mappages(d, (void*) i, PGSIZE, pa, flags) < 0)
			goto bad;

		//increase the ref count
		char *v = P2V(pa);
		inclRefCount(v);
		//shootdown tlb
		invlpg((void *) i);

	}
	return d;

	bad: freevm(d, curproc);
	return 0;
}

// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva) {
	pte_t *pte;

	pte = walkpgdir(pgdir, uva, 0);
	if ((*pte & PTE_P) == 0)
		return 0;
	if ((*pte & PTE_U) == 0)
		return 0;
	return (char*) P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int copyout(pde_t *pgdir, uint va, void *p, uint len) {
	char *buf, *pa0;
	uint n, va0;

	buf = (char*) p;
	while (len > 0) {
		va0 = (uint) PGROUNDDOWN(va);
		pa0 = uva2ka(pgdir, (char*) va0);
		if (pa0 == 0)
			return -1;
		n = PGSIZE - (va - va0);
		if (n > len)
			n = len;
		memmove(pa0 + (va - va0), buf, n);
		len -= n;
		buf += n;
		va = va0 + PGSIZE;
	}
	return 0;
}

extern void *vdso_text_page;
extern vdso_ticks_page_t *vdso_ticks_page;
extern char _binary_vdso_impl_start[], _binary_vdso_impl_size[];

int allocvdso(pde_t *pgdir, struct proc *p) {

	// STEP 1: mapping VDSO code
	// allocate a page for vdso code page, if not already allocated
	// this will be shared across all processes
	if (0 == vdso_text_page) {
		vdso_text_page = kalloc();
		if (!vdso_text_page)
			goto fail;

		// copy the vdso code to the page
		memmove(vdso_text_page, _binary_vdso_impl_start,
				(int) _binary_vdso_impl_size);
	}

	// map the vdso code page to the address space (as read-only)
	if ((int) _binary_vdso_impl_size > PGSIZE)
		panic("vdso text larger than a page");
	if (mappages(pgdir, (void *) VDSOTEXT, PGSIZE, V2P(vdso_text_page), PTE_U)
			< 0)
		goto fail;

	// increment the reference counter because the page is mapped to a new address space
	// YOUR CODE HERE...
	inclRefCount(vdso_text_page);
	// STEP 2: mapping data page for vdso_getpid()
	// allocate a physical page to hold pid
	// there will be a *different* page for each process
	// YOUR CODE HERE...
	vdso_pid_page_t* vdso_pid_page = (vdso_pid_page_t *) kalloc();
	if (!vdso_pid_page)
		goto fail;
	memset(vdso_pid_page, 0, PGSIZE);
	// write the pid to this page
	// YOUR CODE HERE...
	vdso_pid_page->pid = p->pid;
	// map the page at the correct address in the user-mode address space (as read-only)
	// YOUR CODE HERE...
	if (mappages(pgdir, (void *) VDSOTEXT + VDSO_GETPID * PGSIZE, PGSIZE,
			V2P(vdso_pid_page), PTE_U) < 0)
		goto fail;

	// STEP 3: mapping data page for vdso_getticks()
	// allocate a page for ticks page, if not already allocated
	// this page will be *shared* across all processes
	if (0 == vdso_ticks_page) {
		vdso_ticks_page = (vdso_ticks_page_t *) kalloc();
		if (!vdso_ticks_page)
			goto fail;
		memset(vdso_ticks_page, 0, PGSIZE);
	}

	// map the page at the correct address in the user-mode address space (as read-only)
	// YOUR CODE HERE...
	if (mappages(pgdir, (void *) VDSOTEXT + VDSO_GETTICKS * PGSIZE, PGSIZE,
			V2P(vdso_ticks_page), PTE_U) < 0)
		goto fail;
	// increment the reference counter because the page is mapped to a new address space
	// YOUR CODE HERE...
	inclRefCount((void*) vdso_ticks_page);

	return 0;

	fail: return -1;
}

void pagefault() {
	DEBUG("\ndebug: pid %d enter pagefault, addr %x\n", myproc()->pid, rcr2());

	struct trapframe *tf = myproc()->tf;

	if (rcr2() == 0x0) {
		cprintf("NUll Pointer exception -- pid %d %s: trap %d err %d on cpu %d "
				"eip 0x%x addr 0x%x--kill proc\n", myproc()->pid,
				myproc()->name, tf->trapno, tf->err, cpuid(), tf->eip, rcr2());
		myproc()->killed = 1;
		DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
		return;
	}

	pte_t *pte;
	char *a = (char*) PGROUNDDOWN((uint )rcr2());
	if ((pte = walkpgdir(myproc()->pgdir, a, 0)) == 0) {
		cprintf(
				"Invaild address pagefault -- pid %d %s: trap %d err %d on cpu %d "
						"eip 0x%x addr 0x%x--kill proc\n", myproc()->pid,
				myproc()->name, tf->trapno, tf->err, cpuid(), tf->eip, rcr2());
		cprintf(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		myproc()->killed = 1;
		DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
		DEBUG(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		return;
	}

	/*
	 //If FEC_U is not on, fault come from a kernel program
	 if(!(tf->err & FEC_U)){
	 cprintf("Kernel mode pagefault -- unexpected trap %d from cpu %d eip %x (cr2=0x%x)\n",
	 tf->trapno, cpuid(), tf->eip, rcr2());
	 panic("Kernel mode pagefault");
	 }


	 //If FEC_PR is on, kernel address page fault
	 if((tf->err & FEC_PR)){
	 cprintf("Access kernel address pagefault -- pid %d %s: trap %d err %d on cpu %d "
	 "eip 0x%x addr 0x%x--kill proc\n",
	 myproc()->pid, myproc()->name, tf->trapno,
	 tf->err, cpuid(), tf->eip, rcr2());
	 myproc()->killed = 1;
	 return;
	 }
	 */

	//If FEC_WR is not on, this fault is not cause by a write
	if (!(tf->err & FEC_WR)) {
		cprintf("pagefault NOT on write -- pid %d %s: trap %d err %d on cpu %d "
				"eip 0x%x addr 0x%x--kill proc\n", myproc()->pid,
				myproc()->name, tf->trapno, tf->err, cpuid(), tf->eip, rcr2());
		cprintf(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		myproc()->killed = 1;
		DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
		DEBUG(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		return;
	}

	//after check all the error code, 
	//we have a pagefault by write from a user program
	if ((*pte & PTE_COW)) {
		DEBUG("\ndebug: pid %d enter pagefault--COW\n", myproc()->pid);
		DEBUG(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		//Now time to do COW
		uint pa = PTE_ADDR(*pte);
		char *v = P2V(pa);
		uint ref_count = getRefCount(v);

		//If more than 1 reference, hence copy the page
		if (ref_count > 1) {
			DEBUG("\ndebug: pid %d enter pagefault--COW--refcount>1\n",
					myproc()->pid);
			DEBUG(
					"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
					tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
					*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
					tf->esp, myproc()->sz, myproc()->guardpg,
					myproc()->stacktop, myproc()->stacklastpg);

			//get a page
			char* newpg = kalloc();
			//copy all content
			memmove(newpg, v, PGSIZE);
			//get the physical address
			uint phyaddr = V2P(newpg);
			//construct the entry for this proc, other proc's pte is not changed
			//pte is proc dependent
			//allow write, is present, is user mode
			*pte = phyaddr | PTE_FLAGS(*pte) | PTE_P | PTE_W | PTE_U;
			*pte &= ~PTE_COW;
			//shoot down tlb
			invlpg((void *) rcr2());
			declRefCount(v);

			if (!(*pte & PTE_P)) {
				if ( PGROUNDDOWN(rcr2()) != myproc()->guardpg) {
					cprintf(
							"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
							tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
							*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
							*pte & PTE_U, tf->esp, myproc()->sz,
							myproc()->guardpg, myproc()->stacktop,
							myproc()->stacklastpg);

					panic("not guard pg");
				}
				DEBUG(
						"\ndebug: pid %d enter pagefault--COW--refcount>1--guard\n",
						myproc()->pid);
				DEBUG(
						"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
						tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
						*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
						*pte & PTE_U, tf->esp, myproc()->sz, myproc()->guardpg,
						myproc()->stacktop, myproc()->stacklastpg);
				clearpteu(myproc()->pgdir, (char*) (PGROUNDDOWN(rcr2())));
				clearptep(myproc()->pgdir, (char*) (PGROUNDDOWN(rcr2())));
			}
			DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
			DEBUG(
					"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
					tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
					*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
					tf->esp, myproc()->sz, myproc()->guardpg,
					myproc()->stacktop, myproc()->stacklastpg);
			return;
		}
		//Only one reference, let him have it
		else if (ref_count == 1) {
			DEBUG("\ndebug: pid %d enter pagefault--COW--refcount=1\n",
					myproc()->pid);
			DEBUG(
					"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
					tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
					*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
					tf->esp, myproc()->sz, myproc()->guardpg,
					myproc()->stacktop, myproc()->stacklastpg);
			*pte = *pte | PTE_W;
			*pte = *pte & ~PTE_COW;
			//*pte = *pte | PTE_U;
			invlpg((void *) rcr2());
			if (!(*pte & PTE_P) && (*pte & PTE_COW)) {
				if (PGROUNDDOWN(rcr2()) != myproc()->guardpg) {
					cprintf(
							"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
							tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
							*pte & PTE_P, *pte & PTE_U, *pte & PTE_COW,
							*pte & PTE_W, tf->esp, myproc()->sz,
							myproc()->guardpg, myproc()->stacktop,
							myproc()->stacklastpg);

					panic("not guard pg");
				}
				DEBUG(
						"\ndebug: pid %d enter pagefault--COW--refcount=1--guard\n",
						myproc()->pid);
				DEBUG(
						"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
						tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
						*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
						*pte & PTE_U, tf->esp, myproc()->sz, myproc()->guardpg,
						myproc()->stacktop, myproc()->stacklastpg);
				clearpteu(myproc()->pgdir, (char*) (PGROUNDDOWN(rcr2())));
				clearptep(myproc()->pgdir, (char*) (PGROUNDDOWN(rcr2())));
			}
			DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
			DEBUG(
					"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
					tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
					*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
					tf->esp, myproc()->sz, myproc()->guardpg,
					myproc()->stacktop, myproc()->stacklastpg);
			return;
		} else {
			//ref_count is 0, should never happen
			panic("something went really wrong");
		}
	}

	//if PTE_P is not on, is a read only page
	if (!(*pte & PTE_P) && !(*pte & PTE_COW)) {
		DEBUG("\ndebug: pid %d enter pagefault--NOTCOW NOTP\n", myproc()->pid);
		DEBUG(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		//check if should expand stack
		if (rcr2() >= myproc()->stacklastpg && rcr2() < myproc()->stacktop) {
			DEBUG("\ndebug: pid %d enter pagefault--NOTCOW NOTP-instack\n",
					myproc()->pid);
			DEBUG(
					"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
					tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
					*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
					tf->esp, myproc()->sz, myproc()->guardpg,
					myproc()->stacktop, myproc()->stacklastpg);
			//fault inside the stack,expand stack to that location
			int newguardpg = PGROUNDDOWN(rcr2()) - PGSIZE;
			if (newguardpg < myproc()->stacklastpg) {
				cprintf(
						"Stackoverflow, reached stack size limit -- pid %d %s: trap %d err %d on cpu %d "
								"eip 0x%x addr 0x%x--kill proc\n",
						myproc()->pid, myproc()->name, tf->trapno, tf->err,
						cpuid(), tf->eip, rcr2());
				cprintf(
						"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
						tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
						*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
						*pte & PTE_U, tf->esp, myproc()->sz, myproc()->guardpg,
						myproc()->stacktop, myproc()->stacklastpg);
				myproc()->killed = 1;
				DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
				DEBUG(
						"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
						tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
						*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
						*pte & PTE_U, tf->esp, myproc()->sz, myproc()->guardpg,
						myproc()->stacktop, myproc()->stacklastpg);
				return;
			} else {
				DEBUG(
						"\ndebug: pid %d enter pagefault--NOTCOW NOTP-instack-expandstack\n",
						myproc()->pid);
				DEBUG(
						"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
						tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
						*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
						*pte & PTE_U, tf->esp, myproc()->sz, myproc()->guardpg,
						myproc()->stacktop, myproc()->stacklastpg);
				//cprintf("Expanding stack\n");
				//there is space make a guard page
				uint addguard = allocuvm(myproc()->pgdir, newguardpg,
						newguardpg + PGSIZE, myproc());
				if (addguard == 0)
					panic("addguard failed");
				//mark guard pg
				clearpteu(myproc()->pgdir, (char*) (newguardpg));
				clearptep(myproc()->pgdir, (char*) (newguardpg));

				uint oldguardpg = myproc()->guardpg;
				//mark old guard as usable
				markpteup(myproc()->pgdir, (char*) (oldguardpg));

				//add all the page in between curr guard to new guard
				for (int i = addguard; i < oldguardpg; i += PGSIZE) {
					//cprintf("i: %x\n",i);
					uint addpage = allocuvm(myproc()->pgdir, i, i + PGSIZE,
							myproc());
					if (addpage == 0)
						panic("addpage failed");
					markpteup(myproc()->pgdir, (char*) (i));
				}

				//update proc guard gp
				myproc()->guardpg = newguardpg;
				DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
				DEBUG(
						"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
						tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
						*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W,
						*pte & PTE_U, tf->esp, myproc()->sz, myproc()->guardpg,
						myproc()->stacktop, myproc()->stacklastpg);
				return;
			}

		} else {
			cprintf("PTE_P pagefault -- pid %d %s: trap %d err %d on cpu %d "
					"eip 0x%x addr 0x%x--kill proc\n", myproc()->pid,
					myproc()->name, tf->trapno, tf->err, cpuid(), tf->eip,
					rcr2());
			cprintf(
					"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
					tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
					*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
					tf->esp, myproc()->sz, myproc()->guardpg,
					myproc()->stacktop, myproc()->stacklastpg);
			myproc()->killed = 1;
			DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
			DEBUG(
					"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
					tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
					*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
					tf->esp, myproc()->sz, myproc()->guardpg,
					myproc()->stacktop, myproc()->stacklastpg);
			return;
		}
	}

	//if PTE_COW is not on, is a read only page
	if (!(*pte & PTE_COW)) {
		DEBUG("\ndebug: pid %d enter pagefault--NOTCOW\n", myproc()->pid);
		DEBUG(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		cprintf("Read Only pagefault -- pid %d %s: trap %d err %d on cpu %d "
				"eip 0x%x addr 0x%x--kill proc\n", myproc()->pid,
				myproc()->name, tf->trapno, tf->err, cpuid(), tf->eip, rcr2());
		cprintf(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		myproc()->killed = 1;
		DEBUG("\ndebug: pid %d exit pagefault\n", myproc()->pid);
		DEBUG(
				"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
				tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR,
				*pte & PTE_P, *pte & PTE_COW, *pte & PTE_W, *pte & PTE_U,
				tf->esp, myproc()->sz, myproc()->guardpg, myproc()->stacktop,
				myproc()->stacklastpg);
		return;
	}

	cprintf(
			"FEC_U %d, FEC_PR %d, FEC_WR %d, PTE_P %d, PTE_COW %d, PTE_W %d,PTE_U %d, esp %x, myproc()->sz %x, guardpg %x, stacktop %x, stacklast %x\n",
			tf->err & FEC_U, tf->err & FEC_PR, tf->err & FEC_WR, *pte & PTE_P,
			*pte & PTE_U, *pte & PTE_COW, *pte & PTE_W, tf->esp, myproc()->sz,
			myproc()->guardpg, myproc()->stacktop, myproc()->stacklastpg);

	panic("Page fault corner case\n");

}

// Blank page.
// Blank page.
// Blank page.

