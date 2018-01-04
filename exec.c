#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();
  int guardpg=-1;
  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  // leave first page null
  sz = 0;
  if((sz = allocuvm(pgdir, sz, PGSIZE,(struct proc *)0)) == 0)
    goto bad;
  // Mark as not PTE_P
  clearptep(pgdir, 0);


  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz,(struct proc *)0)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

//  // Allocate two pages at the next page boundary.
//  // Make the first inaccessible.  Use the second as the user stack.
// sz = PGROUNDUP(sz);
//  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
//    goto bad;
//  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
//  sp = sz;

  sz = PGROUNDUP(sz);
	// |code|
	//		^
	//	    sz
	
  uint stacklastpg = sz;
  	// |code|
	//		^
	//	stacklastpg
	
  // allocate a 2 pages at the highest stack address, stack grow to smaller address
  guardpg = sz+MAX_STACK+PGSIZE;
  uint stacktop = guardpg+2*PGSIZE;
  sz = allocuvm(pgdir, guardpg, guardpg+2*PGSIZE,(struct proc *)0);
    // |code| [MAX_STACK] | [PGSIZE] | [PGSIZE] | [PGSIZE] |
	//		^						 ^				 	   ^
	//	stacklastpg				  guardpg				 stacktop
	//														sz
  if(sz == 0)
	  panic("panic stackptr failed");
  // guard page
  clearpteu(pgdir, (char*)(guardpg));
  clearptep(pgdir, (char*)(guardpg));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // allocate vdso pages
  allocvdso(pgdir, myproc());

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  //save VMA space for shm

  sz = PGROUNDUP(sz +  MAX_SHM);

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  uint oldguard = curproc->guardpg;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  curproc->guardpg=(uint)guardpg;
  curproc->stacklastpg=stacklastpg;
  curproc->stacktop=stacktop;
  curproc->shmbreak=stacktop;
  curproc->shmnextpg=stacktop;
  curproc->shmtop=sz;

  switchuvm(curproc);
  struct proc dummy;
  (&dummy)->guardpg=oldguard;
  freevm(oldpgdir,&dummy);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir,(struct proc *)0);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
