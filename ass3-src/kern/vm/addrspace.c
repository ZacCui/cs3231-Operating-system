/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *        The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>
#include <synch.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */
#define STACKPAGES    16

/* Allocate/free some kernel-space virtual pages */
struct as_seg *
seg_create(vaddr_t v, size_t s, mode_t m, mode_t bm){
        struct as_seg *seg =kmalloc(sizeof(struct as_seg));
        if(seg == NULL){
            return NULL;
        }
        seg->vbase = v;
        seg->size = s;
        seg->mode = m;
        seg->bk_mode = bm;
        seg->next = NULL;
        return seg;
}

struct addrspace *
as_create(void)
{
    struct addrspace *as;

    as = kmalloc(sizeof(struct addrspace));
    if (as == NULL) {
        return NULL;
    }
    ++as_count;
    as->first = NULL;
    as->asid = as_count<<6;

    return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
    struct addrspace *newas;

    newas = as_create();
    if (newas==NULL) {
        return ENOMEM;
    }

    KASSERT(old->first != NULL);
    newas->first = seg_create(old->first->vbase, old->first->size, old->first->mode, old->first->bk_mode);

    struct as_seg * oldcurrseg = old->first->next;
    struct as_seg * newprevseg = newas->first;

    while(oldcurrseg != NULL){
        struct as_seg *new_seg = seg_create(oldcurrseg->vbase, oldcurrseg->size, oldcurrseg->mode, oldcurrseg->bk_mode);
        if(new_seg == NULL){
            as_destroy(newas);
            return ENOMEM;
        }
        newprevseg->next = new_seg;
        newprevseg = newprevseg->next;
        oldcurrseg = oldcurrseg->next;
    }
    *ret = newas;

    int oldid = old->asid;
    int newid = newas->asid;
    uint32_t i, newframe, oldframe, oldpage, newcount=0, emptycount=0;

    lock_acquire(hpt_lock);
    // enough hpt
    for (i = 0; i < hpt_size; ++i) {
        if ((hpt[i].entryHI & ~PAGE_FRAME) == (uint32_t)oldid)
            ++newcount;
        else if (hpt[i].entryHI==0 && hpt[i].entryLO==0 && hpt[i].next==-1)
            ++emptycount;
    }
    if (emptycount < newcount) {
        lock_release(hpt_lock);
        as_destroy(newas);
        return ENOMEM;
    }
    emptycount = 0;
    // enough frame
    for (i = frame_table_start; i < frame_table_size; ++i)
        if (frame_table[i])
            ++emptycount;
    if (emptycount < newcount) {
        lock_release(hpt_lock);
        as_destroy(newas);
        return ENOMEM;
    }

    for (i = 0; i < hpt_size; ++i) {
        if (hpt[i].entryLO!=0 && (int)(hpt[i].entryHI&~PAGE_FRAME) == oldid) {
            newframe = alloc_kpages_frame() << 12;
            oldpage = hpt[i].entryHI & PAGE_FRAME;
            oldframe = hpt[i].entryLO & PAGE_FRAME;
            memmove((void*)PADDR_TO_KVADDR(newframe), (const void*)PADDR_TO_KVADDR(oldframe), PAGE_SIZE);
            hpt_insert(newas, oldpage | newid, newframe | TLBLO_VALID);
        }
    }


    lock_release(hpt_lock);
    return 0;
}

void
as_destroy(struct addrspace *as)
{
    /*
        * Clean up as needed.
        */
    (void)as;
    struct as_seg * curr = as->first; 
    struct as_seg * prev;
    while(curr != NULL){
        prev = curr;
        curr = curr->next;
        kfree(prev);
    }

    int oldid = as->asid;
    uint32_t i, previndex;
    int nextdelete;
    uint32_t addHI, addLO;
    struct addrspace* addas;

    lock_acquire(hpt_lock);
    for (i = 0; i < hpt_size; ++i) {
        if ((int)(hpt[i].entryHI & ~PAGE_FRAME) == oldid && hpt[i].entryLO!=0) {
            previndex = hpt_hash(as, hpt[i].entryHI);
            if (previndex != i) {
                while (hpt[previndex].next != (int)i) {
                    previndex = hpt[previndex].next;
                }
                hpt[previndex].next = -1;
            }
            nextdelete = hpt[i].next;
            free_kpages_frame(hpt[i].entryLO >> 12);
            hpt[i].entryHI = 0;
            hpt[i].entryLO = 0;
            hpt[i].as = NULL;
            hpt[i].next = -1;
            while (nextdelete != -1) {
                hpt[previndex].next = -1;
                previndex = nextdelete;
                nextdelete = hpt[nextdelete].next;
                addHI = hpt[previndex].entryHI;
                hpt[previndex].entryHI = 0;
                addLO = hpt[previndex].entryLO;
                hpt[previndex].entryLO = 0;
                addas = hpt[previndex].as;
                hpt[previndex].as = NULL;
                hpt[previndex].next = -1;
                hpt_insert(addas, addHI, addLO);
            }
        }
    }
    lock_release(hpt_lock);

    kfree(as);
}

void
as_activate(void)
{
    int i, spl;
    struct addrspace *as;

    as = proc_getas();
    if (as == NULL) {
            /*
                * Kernel thread without an address space; leave the
                * prior address space in place.
                */
            return;
    }

    /*
        * Write this.
        */
    /* Disable interrupts on this CPU while frobbing the TLB. */
    spl = splhigh();
    for (i=0; i<NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);
}

void
as_deactivate(void)
{
    /*
        * Write this. For many designs it won't need to actually do
        * anything. See proc.c for an explanation of why it (might)
        * be needed.
        */
    int i, spl;

    spl = splhigh();
    for (i=0; i<NUM_TLB; i++) {
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    }
    splx(spl);

}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
                 int readable, int writeable, int executable)
{
    /*
        * Write this.
        */
    size_t npages;
    /* Align the region. First, the base... */
    memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
    vaddr &= PAGE_FRAME;

    /* ...and now the length. */
    memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

    npages = memsize / PAGE_SIZE;

    KASSERT(as != NULL);
    struct as_seg * curr = as->first;
    if (curr == NULL) {
        struct as_seg * new = seg_create(vaddr, npages, (readable | writeable | executable), (readable | writeable | executable));
        if(new == NULL){
            return ENOMEM;
        }

        as->first = new;
        return 0;
    }
    while(curr->next != NULL)
        curr = curr->next;

    curr->next = seg_create(vaddr, npages, (readable | writeable | executable), (readable | writeable | executable));

    if(curr->next == NULL)
        return ENOMEM;

    return 0;
}

int
as_prepare_load(struct addrspace *as)
{
    struct as_seg * curr = as->first;
    while(curr != NULL){
        curr->mode |= PF_W; 
        curr = curr->next;
    }
    return 0;
}

int
as_complete_load(struct addrspace *as)
{
    struct as_seg * curr = as->first;
    while(curr != NULL){
        curr->mode = curr->bk_mode;
        curr = curr->next;
    }
    int spl = splhigh();
    for (int i=0; i<NUM_TLB; i++) 
        tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
    splx(spl);
    return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
    /* Initial user-level stack pointer */
    struct as_seg* new = as->first;
    while (new->next)
        new = new->next;
        
    new->next = seg_create(USERSPACETOP - STACKPAGES * PAGE_SIZE, STACKPAGES, PF_R | PF_W | PF_X, PF_R | PF_W | PF_X);
    if (new->next==0) return ENOMEM;

    *stackptr = USERSTACK;

    return 0;
}

