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
// 9 - execute, 10 - write, 11 - read
#define READ_BIT 11
#define WRITE_BIT 10
#define EXEC_BIT 9
#define TEMP_READ_BIT 2
#define TEMP_WRITE_BIT 1
#define TEMP_EXEC_BIT 0


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
			vm_tlbshootdown(get_ind_coremap(pg->paddr), 1);
			page_sneek(pg);
			pa=pg->paddr;
			if(pg->swapped==0){
				page_unlock(pg);
				free_page(pa);
				page_unset_busy(pa);
			}
			else
				page_unlock(pg);
		}

		spinlock_cleanup(&pg->slock);
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
/*
static vaddr_t getfirst10(vaddr_t addr){

	//unsigned int myuint32=0xADADADAD;
	unsigned int highest_10_bits = (addr & (0x1FFFF << (32 - 10))) >> (32 - 10);
	//unsigned int highest_20_bits = (addr & (0x1FFFF << (32 - 20))) >> (32 - 20);
	//unsigned int second_10_bits = (highest_20_bits & (0x3FF));
	return highest_10_bits;
	//printf("\n%d,%d",highest_10_bits,second_10_bits);
}

static vaddr_t getsecond10(vaddr_t addr){

	//unsigned int myuint32=0xADADADAD;
	unsigned int highest_20_bits = (addr & (0x1FFFF << (32 - 20))) >> (32 - 20);
	unsigned int second_10_bits = (highest_20_bits & (0x3FF));
	return second_10_bits;
	//printf("\n%d,%d",highest_10_bits,second_10_bits);
}

static void gettemppermissions(int *a, vaddr_t addr){

	unsigned int last_3 = (addr & (0x6F));
	unsigned int first2 = (last_3 & (0x1FFFF << (3 - 2))) >> (3 - 2);
	unsigned int first = (first2 & (0x1FFFF << (2 - 1))) >> (2 - 1);
	unsigned int second = (first2 & (0x1));
	unsigned int third = (last_3 & (0x1));
	a[0]=first;
	a[1]=second;
	a[2]=third;
}*/
/*static paddr_t ROUNDDOWN(paddr_t size)
{
	if(size%PAGE_SIZE!=0)
	{
	size = ((size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE-1));
	size-=PAGE_SIZE;
	}
	return size;
}
static void getpermissions(int *a, vaddr_t addr){
	unsigned int last_12_bits = (addr & (0xFFF));
	unsigned int final = (last_12_bits & (0x1FFFF << (12 - 3))) >> (12 - 3);
	unsigned int first2 = (final & (0x1FFFF << (3 - 2))) >> (3 - 2);
	unsigned int first = (first2 & (0x1FFFF << (2 - 1))) >> (2 - 1);
	unsigned int second = (first2 & (0x1));
	unsigned int third = (final & (0x1));
	a[0]=first;
	a[1]=second;
	a[2]=third;
}*/

static vaddr_t set_bit(int val, vaddr_t addr, int bit){
	// 9 - execute, 10 - write, 11 - read
	///addr |= (1u << 9);	// set 1 in 9th bit from 0
	if(val==1)
		addr |= (1u << bit);
	else
		addr &= ~(1u << bit);
	return addr;
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
	reg->as_perm=set_bit(readable,reg->as_perm, READ_BIT);
	reg->as_perm=set_bit(writeable,reg->as_perm, WRITE_BIT);
	reg->as_perm=set_bit(executable,reg->as_perm, EXEC_BIT);

	regions_array_add(as->regions, reg, NULL);

	if(regions_array_num(as->regions)==2)
	{
		as->heap_start=vaddr+(PAGE_SIZE);
		as->heap_end=as->heap_start;
		as_define_region(as, as->heap_start, PAGE_SIZE, 1,1,1);
	}

	return 0;
}
/*
static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}*/
/*
static void copy_perm_temp(struct addrspace *as, paddr_t paddr){
	int a[3];
	vaddr_t vaddr;
	getpermissions(a, paddr);
	paddr=set_bit(a[0],paddr,TEMP_READ_BIT);
	paddr=set_bit(a[1],paddr,TEMP_WRITE_BIT);
	paddr=set_bit(a[2],paddr,TEMP_EXEC_BIT);

	paddr=set_bit(1,paddr, READ_BIT);
	paddr=set_bit(1,paddr, WRITE_BIT);

	vaddr=PADDR_TO_KVADDR(paddr);
	int ind=getfirst10(vaddr);
	if(as->pgdir[ind].pg_table==NULL)
			as->pgdir[ind].pg_table=(vaddr_t *)kmalloc(1024*sizeof(vaddr_t));
	as->pgdir[ind].pg_table[getsecond10(vaddr)]=paddr;
}
static void revert_perm_temp(struct addrspace *as, paddr_t paddr){
	int a[3];
	gettemppermissions(a, paddr);
	paddr=set_bit(a[0],paddr,READ_BIT);
	paddr=set_bit(a[1],paddr,WRITE_BIT);
	paddr=set_bit(a[2],paddr,EXEC_BIT);

	paddr=set_bit(0,paddr, TEMP_READ_BIT);
	paddr=set_bit(0,paddr, TEMP_WRITE_BIT);
	paddr=set_bit(0,paddr, TEMP_EXEC_BIT);

	vaddr_t vaddr=PADDR_TO_KVADDR(paddr);
	int ind=getfirst10(vaddr);
	if(as->pgdir[ind].pg_table==NULL)
			as->pgdir[ind].pg_table=(vaddr_t *)kmalloc(1024*sizeof(vaddr_t));
	as->pgdir[ind].pg_table[getsecond10(vaddr)]=paddr;
}
*/
/*static paddr_t getpaddrfromcore(vaddr_t va)
{
	for(int i=0;i<last_index;i++)
	{
		if(core_map[i].vaddr<=va && core_map[i].vaddr+PAGE_SIZE>va)
		{
			core_map[i].pstate=DIRTY;
			core_map[i].npages=1;
			return core_map[i].paddr;
		}
	}
	return 0;
}*/

int
as_prepare_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*revert_perm_temp(as, as->as_pbase1);
	revert_perm_temp(as, as->as_pbase2);
	revert_perm_temp(as, as->as_stackpbase);
*/
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	*stackptr = USERSTACK;
	//struct region *rg=regions_array_get(as->regions,1);

	return as_define_region(as, USERSTACK-(DUMBVM_STACKPAGES*PAGE_SIZE), DUMBVM_STACKPAGES*PAGE_SIZE, 1,1,0);
}

int page_create(struct PTE **ret, paddr_t *retpa)
{
	paddr_t sa,pa;
	struct PTE *pg=kmalloc(sizeof(struct PTE));
	if(pg==NULL){
		panic("Page not allocated: Out of memory");
	}
	spinlock_init(&pg->slock);
	bitmap_alloc(swap_map, &sa);

	pg->saddr=sa;
	pa=alloc_page(pg);
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
