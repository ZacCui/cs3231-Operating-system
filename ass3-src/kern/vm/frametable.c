#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <synch.h>
/* Place your frametable data-structures here 
 * You probably also want to write a frametable initialisation
 * function and call it from vm_bootstrap
 */

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/* Note that this function returns a VIRTUAL address, not a physical 
 * address
 * WARNING: this function gets called very early, before
 * vm_bootstrap().  You may wish to modify main.c to call your
 * frame table initialisation function, or check to see if the
 * frame table has been initialised and call ram_stealmem() otherwise.
 */

uint32_t alloc_kpages_frame() {
    vaddr_t temp = alloc_kpages(0);
    return temp?CONVERT_ADDRESE_FRAME(KVADDR_TO_PADDR(alloc_kpages(0))):0;
}


vaddr_t alloc_kpages(unsigned int npages) {
    /*
    * IMPLEMENT ME.  You should replace this code with a proper
    *                implementation.
    */
    if (frame_table == NULL) {
        paddr_t addr;

        spinlock_acquire(&stealmem_lock);
        addr = ram_stealmem(npages);
        spinlock_release(&stealmem_lock);

        if(addr == 0)
                return 0;

        return PADDR_TO_KVADDR(addr);
    } else {
        uint32_t i = 0;
        spinlock_acquire(&stealmem_lock);
        
        for (i = frame_table_start; i < frame_table_size; ++i) {
            if (frame_table[i]) {
                frame_table[i] = false;
                bzero((void*)PADDR_TO_KVADDR(CONVERT_FRAME_ADDRESE(i)), PAGE_SIZE);
                spinlock_release(&stealmem_lock);
                return PADDR_TO_KVADDR(CONVERT_FRAME_ADDRESE(i));
            }
        }
        spinlock_release(&stealmem_lock);
        return 0;
    }

    panic("AMAZING!! How did you get here??");
}

void free_kpages_frame(uint32_t frame) {
    free_kpages(PADDR_TO_KVADDR(CONVERT_FRAME_ADDRESE(frame)));
}


void free_kpages(vaddr_t addr) {
    spinlock_acquire(&stealmem_lock);
    addr = addr & PAGE_FRAME;
    frame_table[CONVERT_ADDRESE_FRAME(KVADDR_TO_PADDR(addr))] = true;
    bzero((void*)addr, PAGE_SIZE);
    spinlock_release(&stealmem_lock);
}

