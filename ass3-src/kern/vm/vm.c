#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <current.h>
#include <proc.h>
#include <spl.h>
#include <synch.h>

/* Place your page table functions here */

bool hpt_insert(struct addrspace *as, vaddr_t hi, paddr_t lo) {
    uint32_t newprev = hpt_hash(as, hi);
    if (hpt[newprev].entryLO == 0) {
        hpt[newprev].entryHI = hi;
        hpt[newprev].entryLO = lo;
        hpt[newprev].as = as;
        return false;
    }
    while (hpt[newprev].next != -1) {
        newprev = hpt[newprev].next;
    }
    for (uint32_t newindex = 0; newindex < hpt_size; ++newindex) {
        if (hpt[newindex].entryHI==0 && hpt[newindex].entryLO==0 && hpt[newindex].next==-1) {
            hpt[newindex].entryHI = hi;
            hpt[newindex].entryLO = lo;
            hpt[newindex].as = as;
            hpt[newprev].next = newindex;
            return false;
        }
    }
    return true;
}

uint32_t hpt_hash(struct addrspace *as, vaddr_t faultaddr)
{
    uint32_t index;
    index = (((uint32_t )as) ^ (faultaddr >> 12)) % hpt_size;
    return index;
}

void vm_bootstrap(void)
{
    /* Initialise VM sub-system.  You probably want to initialise your 
        frame table here as well.
    */
    as_count = 0;
    hpt_lock = lock_create("hpt_lock");
    uint32_t temp_size = ram_getsize();
    frame_table_size = temp_size / PAGE_SIZE;
    hpt_size = 2 * frame_table_size;
    hpt = kmalloc(hpt_size * sizeof(struct hash_page_table));
    frame_table = kmalloc(frame_table_size);
    frame_table_start = 1 + ram_getfirstfree() / PAGE_SIZE;

    lock_acquire(hpt_lock);
    for (uint32_t i = 0; i < hpt_size; ++i) {
        hpt[i].entryHI = 0;
        hpt[i].entryLO = 0;
        hpt[i].as = 0;
        hpt[i].next = -1;
    }
    lock_release(hpt_lock);
    for (uint32_t i = 0; i < frame_table_start; ++i)
        frame_table[i] = false;
    for (uint32_t i = frame_table_start; i < frame_table_size; ++i)
        frame_table[i] = true;
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace *as;
	int spl;
	faultaddress &= PAGE_FRAME;

    switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		    return EFAULT;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

    if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
    as_seg curr = as->first;
    bool notfound = true;
    mode_t dirtybit = 0;
    while (curr) {
        if ((curr->vbase & PAGE_FRAME) <= faultaddress && ((curr->vbase>>12) + curr->size)<<12 > faultaddress) {
            notfound = false;
            dirtybit = curr->mode;
            break;
        }
        curr = curr->next;
    }

    // if not in address space region
    if (notfound)
        return EFAULT;
    lock_acquire(hpt_lock);
	// calculate have privillage
    dirtybit = (dirtybit & 2) ? TLBLO_DIRTY:0;
    dirtybit |= TLBLO_VALID;

    // if in hpt
    faultaddress |= as->asid;
    uint32_t hi = hpt_hash(as, faultaddress);
    while (1) {
        if (hpt[hi].entryHI == faultaddress && hpt[hi].entryLO != 0) {
            spl = splhigh();
            tlb_random(hpt[hi].entryHI, hpt[hi].entryLO|dirtybit);
            splx(spl);
            lock_release(hpt_lock);
            return 0;
        } else if (hpt[hi].entryLO != 0 && hpt[hi].next != -1) {
            hi = hpt[hi].next;
        } else {
            break;
        }
    }
    // if not
    uint32_t newframe = alloc_kpages_frame();
    newframe = newframe<<12;
    if (newframe==0) {

        lock_release(hpt_lock);

        kprintf("Ran out of TLB entries - cannot handle page fault\n");

        return EFAULT;
    }

    if (hpt_insert(as, faultaddress, newframe|TLBLO_VALID)) {
        free_kpages_frame(newframe>>12);
            lock_release(hpt_lock);
            return EFAULT;
    }

    spl = splhigh();
    tlb_random(faultaddress, newframe|dirtybit);
    splx(spl);
    lock_release(hpt_lock);
    return 0;

}

/*
*
* SMP-specific functions.  Unused in our configuration.
*/

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
    (void)ts;
    panic("vm tried to do tlb shootdown?!\n");
}

