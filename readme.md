## Lab 1: Getting started with xv6
https://compas.cs.stonybrook.edu/~nhonarmand/courses/fa17/cse306/lab1.html

	

## Lab 2: COW fork() in xv6
https://compas.cs.stonybrook.edu/~nhonarmand/courses/fa17/cse306/lab2.html
### Readme
    Lab2 Copy-on-Write Fork

    kalloc.c:
        -A speical kfree() named kfree2() was writen for freerange use only. 
        Since asserting reference count at that stage can be zero.
        -Atomic instruc were used to inclement and decrement the reference count.
    trap.c:
        -A page fault handler was registered. The handler function is in vm.c
    vm.c:
        -deallocuvm() was changed to only call kfree with reference count is one.
        -cowuvm() implemented page mapping part of copy-on-write.
        If the page is already read only, dont flag PTE_COW. Program should be killed on write.
        -pagefault() is the page fault handler. FEC_U and FEC_PD checks were commented out for causing issues.
        When a page fault occur, if it is cause by writing on a read only page with PTE_COW bit on, 
        if reference count >, alloc page and copy. If reference = 1, allow write and remove PTE_COW flag. 
    forktest.c
        -three additional tests were added.
            1. child exec a program
            2. write after fork, force page copying to occur
            3. intentionally trigger page fault to test correctness.



## Lab 3: Advanced virtual memory tricks in xv6
https://compas.cs.stonybrook.edu/~nhonarmand/courses/fa17/cse306/lab3.html
### Readme

    Part1:

        For all process, page address 0 (first page address) PTE_P is set to 0
        When pagefault trigger for that page, the process will be killed for null pointer

    Part2:

        exec is changed such that:
            The memory layout of any process will be:
            |null page|code|stack|heap|
            inside stack:
            |reserved address space|guardpg|stack|
            
        the proc->sz will include all the space inside stack including reserved spaces.

        proc->guardpg, proc->stacktop, proc->stacklastpg
        are added to keep track of stack info.

        Functions like cowuvm will skip the pages that are in the reserved spaces.

        The stack pages and the guard page are copy on fork

        All syscall arg checking functions like argptr will reject any address in the 
        guard page and reserved spaces
        special treatment was given to the init process for the those checking
        
        If the new guard page address needed for expanding stack is < proc->stacklastpg, 
        processes will be killed for stackoverflow
        
    Part3:

        Nothing special was done. Followed instructions.
        
        
    unittestlab3.c includes all the unit test i have written for all parts of this lab.
    DEBUG marco is added for debugging, DEBUGPRINT is set to 0 for submission.

    usertests was ran

## Lab 4: Synchronization
https://compas.cs.stonybrook.edu/~nhonarmand/courses/fa17/cse306/lab4.html
### Readme
    PART1:
        1.save VMA at exec for shm
        2.for functions in vm.c, include the cases regarding shm space.
            (not allocated, allocated space)
        3.ref count increased and mapped to child on fork
        4.syscall on shm space that is not allocated will be rejected
        5.pte flag is not used. Rather, shm status are keep track in proc structs.
        6.that proc info is copy over on fork
        
    PART2:
        1.for futex wait, ptable.lockand sleep(loc, &ptable.lock) is used
        2.futex_wake will return number of process waked. 
            wakeupfutex and wakeupfutex1 are created to make that happen
        3.using ptable.lock will ensure atomicity since futex_wake and futex_wait 
            will compete over the lock
        4.no deadlock were found in run futextest.c
        
    PART3:
        1.test and set is used to implement mutex.
            1 is locked and 0 is unlocked
            __sync_lock_test_and_set will ensure the change between 0 to 1 is atomic
        2.cond_var_t contains a mutex (cvslock) to ensure atomicity
        3.cv_wait will grab the cvslock before releasing the mutex.
            since cv_bcast also need cvslock to call futex_wake, cvslock ensure
            bcast can not happen until cv_wait finshed.
        4.cv_wait calls futex_wait2, a new syscall like futex_wait but will release 
            cvslock and wake processes waiting on cvslock after grabbing ptable.lock.
            wakeupfutex2 is created for the this especial case where the ptable is 
            alreadt held on calling wake. 
            Ather the release of the cvslock, cv_bcast will call futex_wake which 
            will call wakeupfutex which need ptable.lock to process. Therefore 
            atomicity between release the mutex and sleeping is ensured
        5.checksum.c first calcuate the checksum by a single process. Then 
            4 producers and 4 consumers process are created and the numbers agreed.


