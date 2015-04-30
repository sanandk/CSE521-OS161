/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
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
#include <array.h>
#include <addrspace.h>
#include <bitmap.h>
#include <synch.h>
#include <spl.h>
#include <spinlock.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>
DEFARRAY_BYTYPE(regions_array, struct region,);


#define DUMBVM_STACKPAGES    12

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
struct addrspace *
as_create(void)
{
	struct addrspace *as = (struct addrspace*) kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
	as->regions=regions_array_create();
	if(as->regions==NULL)
	{
		kfree(as);
		return NULL;
	}
	return as;
}
static void region_destroy(struct region *reg)
{
	paddr_t pa;
	struct PTE *pg;
	for(int i=0;i<(int)pagetable_array_num(reg->pages);i++)
	{
		pg=pagetable_array_get(reg->pages,i);
		if(pg!=NULL){
			tlb_shootbyvaddr(pg->paddr + (PAGE_SIZE * i));
			page_sneek(pg);
			pa=pg->paddr & PAGE_FRAME;
			if(pg->swapped==0){
				page_unlock(pg);
				free_page(pa);
				page_unset_busy(pa);
			}
			else
				page_unlock(pg);
			spinlock_cleanup(&pg->slock);
		}
		kfree(pg);
	}
	pagetable_array_setsize(reg->pages, 0);
}
void
as_destroy(struct addrspace *as)
{
	struct region *reg;

	for(int i=0;i<(int)regions_array_num(as->regions);i++)
	{
		reg=regions_array_get(as->regions, i);
		region_destroy(reg);
	}
	regions_array_setsize(as->regions, 0);
	regions_array_destroy(as->regions);
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	(void)as;
		int i,spl;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<64; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

static struct region* region_create(int npages){
	struct region *reg;
	reg= kmalloc(sizeof(struct region));
	if(reg==NULL){
		panic("Region cannot be created!");
	}
	reg->as_npages=npages;
	reg->pages=pagetable_array_create();
	KASSERT(reg->pages!=NULL);

	int i=pagetable_array_setsize(reg->pages, npages);
	KASSERT(i==0);
	for(i=0;i<npages;i++){
		pagetable_array_set(reg->pages, i, NULL);
	}
	return reg;
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	(void)readable;
	(void)writeable;
	(void)executable;
	struct region *reg;
	size_t npages;
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = sz / PAGE_SIZE;

	reg=region_create(npages);
	if(reg==NULL){
		return ENOMEM;
	}
	reg->as_npages=npages;
	reg->as_vbase=vaddr;
	reg->as_perm=0;

	regions_array_add(as->regions, reg, NULL);
	int num=regions_array_num(as->regions);
	if(num==2)
	{
		as->heap_start=vaddr;
		as->heap_end=as->heap_start;
		struct region *heap=region_create(1);
		if(heap==NULL){
			return ENOMEM;
		}
		heap->as_npages=1;
		heap->as_vbase=vaddr;
		heap->as_perm=0;
		regions_array_add(as->regions, heap, NULL);

		struct region *stack=region_create(DUMBVM_STACKPAGES);
		if(stack==NULL){
			return ENOMEM;
		}
		stack->as_npages=DUMBVM_STACKPAGES;
		stack->as_vbase=USERSTACK-(DUMBVM_STACKPAGES*PAGE_SIZE);
		stack->as_perm=0;
		regions_array_add(as->regions, stack, NULL);
	}

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	*stackptr = USERSTACK;
	(void)as;
	return 0;
}

int page_create(struct PTE **ret, paddr_t *retpa)
{
	paddr_t pa;
	struct PTE *pg=kmalloc(sizeof(struct PTE));
	if(pg==NULL){
		panic("Page not allocated: Out of memory");
	}
	spinlock_init(&pg->slock);

	pa=alloc_page(pg);

//		int res=bitmap_alloc(swap_map, &sa);
	spinlock_acquire(&swap_address_lock);
	lastsa++;
	pg->saddr=lastsa*PAGE_SIZE;
	//kprintf("\nAllocated sa=%d, %x",(int)lastsa, (unsigned int)pa);
	if(lastsa>=total_swap)
		panic("SWAP OUT OF MEMORY!");
	spinlock_release(&swap_address_lock);
	pg->swapped=0;

	KASSERT(pa!=0);

	page_lock(pg);
	pg->paddr=pa;
	KASSERT(is_busy(pa));

	*ret=pg;
	*retpa=pa;

	return 0;
}

static int page_copy(struct PTE *old, struct PTE **ret)
{
	struct PTE *new;
	paddr_t oldpa, newpa,sa;

	page_create(&new, &newpa);
	KASSERT(is_busy(newpa));
	page_sneek(old);
	oldpa=old->paddr & PAGE_FRAME;
	if(old->swapped==1){
		sa=old->saddr;
		page_unlock(old);
		oldpa=alloc_page(old);
		KASSERT(oldpa!=0);
		KASSERT(is_busy(oldpa));
		lock_acquire(biglock_paging);
		swapin(oldpa, sa);
		page_lock(old);
		lock_release(biglock_paging);
		old->swapped=0;
		old->paddr=oldpa;
	}
	KASSERT(is_busy(oldpa));

	memmove((void *)PADDR_TO_KVADDR(newpa),
			(const void *)PADDR_TO_KVADDR(oldpa),
			PAGE_SIZE);

	page_unlock(old);
	page_unlock(new);

	page_unset_busy(newpa);
	page_unset_busy(oldpa);
	*ret=new;

	return 0;
}

static int region_copy(struct region *old, struct region **ret)
{
	struct region *new;
	struct PTE *newpg, *oldpg;

	new=region_create(old->as_npages);
	KASSERT(new!=NULL);
	new->as_vbase=old->as_vbase;
	for(int i=0;i<(int)old->as_npages;i++){
		oldpg=pagetable_array_get(old->pages, i);
		newpg=pagetable_array_get(new->pages, i);
		KASSERT(newpg==NULL);
		if(oldpg!=NULL){
			page_copy(oldpg, &newpg);
		}
		pagetable_array_set(new->pages, i, newpg);
	}
	*ret=new;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;
	struct region *reg, *nreg;
	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}
	for(int i=0;i<(int)regions_array_num(old->regions);i++)
	{
		reg=regions_array_get(old->regions,i);
		region_copy(reg, &nreg);
		regions_array_add(new->regions, nreg, NULL);
	}
	*ret = new;
	return 0;
}
