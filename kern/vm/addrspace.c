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
	/*struct addrspace *as = kmalloc(sizeof(struct regions));
	if (as==NULL) {
		return NULL;
	}*/
	//as->pgdir=(struct PTE*)kmalloc(1024*sizeof(struct PTE));
	struct addrspace *as = (struct addrspace*) kmalloc(sizeof(struct addrspace));
	as->pages=NULL;
	as->as_vbase = 0;
	as->as_pbase = 0;
	as->as_npages = 0;
	as->stack = NULL;
	as->heap = NULL;
	as->next=NULL;
	return as;
}

void
as_destroy(struct addrspace *as)
{
	struct addrspace *temp,*temp1;
	struct PTE *ptemp,*temp2;

	ptemp=as->heap->pages;
	free_page(ptemp->paddr);
	//kfree(ptemp);
	//kfree(as->heap);
	temp=as->stack;
	while(temp!=NULL)
	{
				ptemp=temp->pages;
				while(ptemp!=NULL){
					temp2=ptemp;
					free_page(ptemp->paddr);
					ptemp=ptemp->next;
			//		kfree(temp2);
				}
				temp1=temp;
				temp=temp->next;
		//		kfree(temp1);
	}
	temp=as;
	while(temp!=NULL)
	{
			ptemp=temp->pages;
			while(ptemp!=NULL){
				temp2=ptemp;
				free_page(ptemp->paddr);
				ptemp=ptemp->next;
	//			kfree(temp2);
			}
			temp1=temp;
			temp=temp->next;
//			kfree(temp1);
	}

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

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	struct addrspace *temp,*reg;
	size_t npages;
	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;
	npages = sz / PAGE_SIZE;

	if(as==NULL || as->as_npages==0){
		as->as_npages=npages;
		as->as_vbase=vaddr;
		as->as_perm=0;
		as->as_perm=set_bit(readable,as->as_perm, READ_BIT);
		as->as_perm=set_bit(writeable,as->as_perm, WRITE_BIT);
		as->as_perm=set_bit(executable,as->as_perm, EXEC_BIT);
		as->next=NULL;
		as->pages=NULL;
	}
	else{
		temp=as;
		while(temp->next!=NULL)
			temp=temp->next;
		reg = (struct addrspace*) kmalloc(sizeof(struct addrspace));
		reg->as_npages=npages;
		reg->as_vbase=vaddr;
		reg->as_perm=0;
		reg->as_perm=set_bit(readable,as->as_perm, READ_BIT);
		reg->as_perm=set_bit(writeable,as->as_perm, WRITE_BIT);
		reg->as_perm=set_bit(executable,as->as_perm, EXEC_BIT);
		reg->pages=NULL;
		reg->next=NULL;
		temp->next=reg;
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
	/*KASSERT(as->as_pbase1 == 0);
	KASSERT(as->as_pbase2 == 0);
	KASSERT(as->as_stackpbase == 0);*/
	paddr_t pa;
	int i;
	vaddr_t va;
	struct addrspace *temp=as;
	struct PTE *ptemp,*pg;
	while(temp!=NULL)
	{
		va=temp->as_vbase;
		for(i=0;i<(int)temp->as_npages;i++)
		{
			if(temp->pages==NULL){
				temp->pages=(struct PTE *)kmalloc(sizeof(struct PTE));
				temp->pages->next=NULL;
				temp->pages->perm=temp->as_perm;
				temp->pages->vaddr=va;
				pa=alloc_page();
				if(pa==0)
					return ENOMEM;
				temp->pages->paddr=pa;
			}
			else{
				ptemp=temp->pages;
				while(ptemp->next!=NULL)
					ptemp=ptemp->next;
				pg=(struct PTE *)kmalloc(sizeof(struct PTE));
				pg->next=NULL;
				pg->perm=as->as_perm;
				pg->vaddr=va;
				pa=alloc_page();
				if(pa==0)
					return ENOMEM;
				pg->paddr=pa;
				ptemp->next=pg;
			}
			va+=PAGE_SIZE;
		}
		temp=temp->next;
	}

	as->heap = (struct addrspace*) kmalloc(sizeof(struct addrspace));
	as->heap->as_npages=1;
	as->heap->as_vbase=va;
	as->heap->as_perm=0;
	as->heap->next=NULL;
	as->heap->pages=(struct PTE *)kmalloc(sizeof(struct PTE));
	as->heap->pages->next=NULL;
	as->heap->pages->perm=0;
	as->heap->pages->vaddr=va;
	pa=alloc_page();
	if(pa==0)
		return ENOMEM;
	as->heap->pages->paddr=pa;

	vaddr_t s_va=USERSTACK-(DUMBVM_STACKPAGES*PAGE_SIZE);
	as->stack = (struct addrspace*) kmalloc(sizeof(struct addrspace));
	as->stack->as_npages=DUMBVM_STACKPAGES;
	as->stack->as_vbase=s_va;
	as->stack->as_perm=0;
	as->stack->next=NULL;
	as->stack->pages=NULL;

	for(int i=0;i<DUMBVM_STACKPAGES;i++)
	{
		if(as->stack->pages==NULL)
		{
			as->stack->pages=(struct PTE *)kmalloc(sizeof(struct PTE));
			as->stack->pages->next=NULL;
			as->stack->pages->perm=0;
			as->stack->pages->vaddr=s_va;
			pa=alloc_page();
			if(pa==0)
				return ENOMEM;
			as->stack->pages->paddr=pa;
		}
		else
		{
			ptemp=as->stack->pages;
			while(ptemp->next!=NULL)
				ptemp=ptemp->next;
			pg=(struct PTE *)kmalloc(sizeof(struct PTE));
			pg->next=NULL;
			pg->perm=0;
			pg->vaddr=s_va;
			pa=alloc_page();
			if(pa==0)
				return ENOMEM;
			pg->paddr=pa;
			ptemp->next=pg;
		}
		s_va+=PAGE_SIZE;
	}

	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*revert_perm_temp(as, as->as_pbase1);
	revert_perm_temp(as, as->as_pbase2);
	revert_perm_temp(as, as->as_stackpbase);
*/
	as->pid=curthread->process_id;
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

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *new;

	new = as_create();
	if (new==NULL) {
		return ENOMEM;
	}

	struct addrspace *otemp=old, *ntemp, *reg;
	while(otemp!=NULL)
	{

		if(new==NULL || new->as_npages==0){
				ntemp = (struct addrspace*) kmalloc(sizeof(struct addrspace));
				ntemp->as_npages=otemp->as_npages;
				ntemp->pages=NULL;
				ntemp->as_vbase=otemp->as_vbase;
				ntemp->as_pbase=otemp->as_pbase;
				ntemp->as_perm=otemp->as_perm;
				ntemp->next=NULL;
				new=ntemp;
			}
			else{
				ntemp=new;
				while(ntemp->next!=NULL)
					ntemp=ntemp->next;
				reg = (struct addrspace*) kmalloc(sizeof(struct addrspace));
				reg->as_npages=otemp->as_npages;
				reg->as_vbase=otemp->as_vbase;
				reg->as_pbase=otemp->as_pbase;
				reg->pages=NULL;
				reg->as_perm=otemp->as_perm;
				reg->next=NULL;
				ntemp->next=reg;
			}
		otemp=otemp->next;
	}

	new->heap = (struct addrspace*) kmalloc(sizeof(struct addrspace));
	new->heap->as_npages=old->heap->as_npages;
	new->heap->as_vbase=old->heap->as_vbase;
	new->heap->as_perm=old->heap->as_perm;
	new->heap->next=NULL;
	new->heap->pages=(struct PTE *)kmalloc(sizeof(struct PTE));
	new->heap->pages->next=NULL;
	new->heap->pages->perm=old->heap->pages->perm;
	new->heap->pages->vaddr=old->heap->pages->vaddr;
	new->heap->pages->paddr=old->heap->pages->paddr;

	new->stack = (struct addrspace*) kmalloc(sizeof(struct addrspace));
	new->stack->as_npages=old->stack->as_npages;
	new->stack->as_vbase=old->stack->as_vbase;
	new->stack->as_perm=old->stack->as_perm;
	new->stack->next=NULL;
	new->stack->pages=NULL;


	/* (Mis)use as_prepare_load to allocate some physical memory. */
	if (as_prepare_load(new)) {
		as_destroy(new);
		return ENOMEM;
	}

	struct PTE *temp1=old->pages,*temp2=new->pages;

	while(temp1!=NULL)
	{
		memmove((void *)PADDR_TO_KVADDR(temp2->paddr),
						(const void *)PADDR_TO_KVADDR(temp1->paddr),
						PAGE_SIZE);
		/*memmove((void *)PADDR_TO_KVADDR(temp2->vaddr),
								(const void *)PADDR_TO_KVADDR(temp1->vaddr),
								PAGE_SIZE);
		memmove((void *)PADDR_TO_KVADDR(temp2->perm),
								(const void *)PADDR_TO_KVADDR(temp1->perm),
								sizeof(int));*/
		temp1=temp1->next;
		temp2=temp2->next;
	}

	temp1=old->stack->pages;
	temp2=new->stack->pages;

	while(temp1!=NULL)
	{
		memmove((void *)PADDR_TO_KVADDR(temp2->paddr),
						(const void *)PADDR_TO_KVADDR(temp1->paddr),
						PAGE_SIZE);
		temp1=temp1->next;
		temp2=temp2->next;
	}
	*ret = new;
	return 0;
}
