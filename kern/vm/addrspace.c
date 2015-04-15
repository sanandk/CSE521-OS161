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
#include <addrspace.h>


#include <spl.h>
#include <spinlock.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
#include <vm.h>
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
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}
	//as->pgdir=(struct PTE*)kmalloc(1024*sizeof(struct PTE));
	as->pgdir=NULL;
	as->as_vbase1 = 0;
	as->as_pbase1 = 0;
	as->as_npages1 = 0;
	as->as_vbase2 = 0;
	as->as_pbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpbase = 0;
	as->as_heap_start = 0;
	as->as_heap_end = 0;

	return as;
}

void
as_destroy(struct addrspace *as)
{
	kfree(as);
}

void
as_activate(struct addrspace *as)
{
	int i,spl;

	(void)as;

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
static paddr_t ROUNDDOWN(paddr_t size)
{
	if(size%PAGE_SIZE!=0)
	{
	size = ((size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE-1));
	size-=PAGE_SIZE;
	}
	return size;
}/*
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

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages;
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = sz / PAGE_SIZE;

	if (as->as_vbase1 == 0) {
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		as->as_perm1=0;
		as->as_perm1=set_bit(readable,as->as_perm1, READ_BIT);
		as->as_perm1=set_bit(writeable,as->as_perm1, WRITE_BIT);
		as->as_perm1=set_bit(executable,as->as_perm1, EXEC_BIT);

		/*
		paddr_t PPN=as->as_pbase1;
		PPN=set_bit(readable,PPN, READ_BIT);
		PPN=set_bit(writeable,PPN, WRITE_BIT);
		PPN=set_bit(executable,PPN, EXEC_BIT);
		int ind=getfirst10(vaddr);
		if(as->pgdir[ind].pg_table==NULL)
			as->pgdir[ind].pg_table=(vaddr_t *)kmalloc(1024*sizeof(vaddr_t));

		as->pgdir[ind].pg_table[getsecond10(vaddr)]=PPN;
*/
		return 0;
	}

	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->as_perm2=0;
		as->as_perm2=set_bit(readable,as->as_perm1, READ_BIT);
		as->as_perm2=set_bit(writeable,as->as_perm1, WRITE_BIT);
		as->as_perm2=set_bit(executable,as->as_perm1, EXEC_BIT);

	/*	paddr_t PPN=as->as_pbase2;
		PPN=set_bit(readable,PPN, READ_BIT);
		PPN=set_bit(writeable,PPN, WRITE_BIT);
		PPN=set_bit(executable,PPN, EXEC_BIT);
		int ind=getfirst10(vaddr);
		if(as->pgdir[ind].pg_table==NULL)
			as->pgdir[ind].pg_table=(vaddr_t *)kmalloc(1024*sizeof(vaddr_t));
		as->pgdir[ind].pg_table[getsecond10(vaddr)]=PPN;
*/
		as->as_heap_start=ROUNDDOWN(vaddr + (npages*PAGE_SIZE));
		return 0;
	}

	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return EUNIMP;
}

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}
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
	KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);
	int i;
	paddr_t pa;
	vaddr_t va=as->as_vbase1;
	struct PTE *pg,*temp;
	for(i=0;i<(int)as->as_npages1;i++)
	{
		if(as->pgdir==NULL){
			as->pgdir=(struct PTE *)kmalloc(sizeof(struct PTE));
			as->pgdir->next=NULL;
			as->pgdir->perm=as->as_perm1;
			as->pgdir->vaddr=va;
			pa=alloc_page();
			if(pa==0)
				return ENOMEM;
			as->pgdir->paddr=pa;
			}
		else{
			temp=as->pgdir;
			while(temp->next!=NULL)
				temp=temp->next;
			pg=(struct PTE *)kmalloc(sizeof(struct PTE));
			pg->next=NULL;
			pg->perm=as->as_perm1;
			pg->vaddr=va;
			pa=alloc_page();
			if(pa==0)
				return ENOMEM;
			pg->paddr=pa;
			temp->next=pg;
		}
		va+=PAGE_SIZE;
		if(as->as_pbase1==0)
			as->as_pbase1=pa;
	}
	va=as->as_vbase2;
	for(i=0;i<(int)as->as_npages2;i++)
	{
		if(as->pgdir==NULL){
			as->pgdir=(struct PTE *)kmalloc(sizeof(struct PTE));
			as->pgdir->next=NULL;
			as->pgdir->perm=as->as_perm2;
			as->pgdir->vaddr=va;
			pa=alloc_page();
			if(pa==0)
				return ENOMEM;
			as->pgdir->paddr=pa;
			}
		else{
			temp=as->pgdir;
			while(temp->next!=NULL)
				temp=temp->next;
			pg=(struct PTE *)kmalloc(sizeof(struct PTE));
			pg->next=NULL;
			pg->perm=as->as_perm2;
			pg->vaddr=va;
			pa=alloc_page();
			if(pa==0)
				return ENOMEM;
			pg->paddr=pa;
			temp->next=pg;
		}
		va+=PAGE_SIZE;
		if(as->as_pbase2==0)
			as->as_pbase2=pa;
	}

	va=USERSTACK-(DUMBVM_STACKPAGES*PAGE_SIZE);

	for(int i=0;i<DUMBVM_STACKPAGES;i++)
	{
		temp=as->pgdir;
		while(temp->next!=NULL)
			temp=temp->next;
		pg=(struct PTE *)kmalloc(sizeof(struct PTE));
		pg->next=NULL;
		pg->perm=as->as_perm1;
		pg->vaddr=va;
		pa=alloc_page();
		if(pa==0)
			return ENOMEM;
		pg->paddr=pa;
		temp->next=pg;
		va+=PAGE_SIZE;
	}

	as_zero_region(as->as_pbase1, as->as_npages1);
	as_zero_region(as->as_pbase2, as->as_npages2);
	as_zero_region(as->as_stackpbase, DUMBVM_STACKPAGES);

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
	KASSERT(as->as_stackpbase != 0);
	as->as_stackpbase=USERSTACK;
	*stackptr = USERSTACK;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;

	new->as_heap_start=old->as_heap_start;
	new->as_heap_end=old->as_heap_end;
	new->pgdir=old->pgdir;
	/*for(int i=0;i<1024;i++)
	{
		for(int j=0;j<1024;j++)
		{
			(new->pgdir[i]).pg_table[j] =(old->pgdir[i]).pg_table[j];
		}
	}*/

	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	KASSERT(new->as_pbase1 != 0);
	KASSERT(new->as_pbase2 != 0);
	KASSERT(new->as_stackpbase != 0);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase1),
		(const void *)PADDR_TO_KVADDR(old->as_pbase1),
		old->as_npages1*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_pbase2),
		(const void *)PADDR_TO_KVADDR(old->as_pbase2),
		old->as_npages2*PAGE_SIZE);

	memmove((void *)PADDR_TO_KVADDR(new->as_stackpbase),
		(const void *)PADDR_TO_KVADDR(old->as_stackpbase),
		DUMBVM_STACKPAGES*PAGE_SIZE);
	
	*ret = new;
	return 0;
}
