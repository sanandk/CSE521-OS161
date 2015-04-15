#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <thread.h>
#include <synch.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>


#define DUMBVM_STACKPAGES    12

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct lock *coremap_lock;
static int vm_bootstrapped=0;
static paddr_t freeaddr;
static paddr_t ROUNDDOWN(paddr_t size)
{
	if(size%PAGE_SIZE!=0)
	{
	size = ((size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE-1));
	size-=PAGE_SIZE;
	}
	return size;
}
static int getpageindex(paddr_t addr)
{
	return ROUNDDOWN(addr)/PAGE_SIZE;
}

void
vm_bootstrap(void)
{
	coremap_lock = lock_create("core_map");
	paddr_t first_addr, lastaddr;
	ram_getsize(&first_addr, &lastaddr);
	int total_page_num = (lastaddr-first_addr) / PAGE_SIZE;
	/* pages should be a kernel virtual address !!  */
	core_map = (struct coremap_page*)PADDR_TO_KVADDR(first_addr);
	freeaddr = first_addr + total_page_num * sizeof(struct coremap_page);
	freeaddr=ROUNDDOWN(freeaddr);

	kprintf("\n%d,%x,%x,%d\n",ROUNDDOWN(lastaddr-freeaddr) / PAGE_SIZE,first_addr,lastaddr,total_page_num);

	// Set page as fixed for paddr 0 to freeaddr
	free_index=getpageindex(freeaddr);
	last_index=getpageindex(lastaddr);
	last_index=total_page_num;
	for(int i=0;i<total_page_num;i++)
	{
		core_map[i].paddr= (PAGE_SIZE*i) +freeaddr;
		core_map[i].vaddr=PADDR_TO_KVADDR(core_map[i].paddr);
		//kprintf("%x\t",core_map[i].paddr);
		//kprintf("%x\t",core_map[i].vaddr);
		core_map[i].pstate=FREE;
	}
	vm_bootstrapped=1;
	//kprintf("%d,%d,%d",getpageindex(0),getpageindex(freeaddr),getpageindex(lastaddr));


}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_page(void)
{
	paddr_t pa;
	spinlock_acquire(&stealmem_lock);
	int found=-1;
	for(int i=0;i<last_index;i++)
	{
		if(core_map[i].pstate==FREE)
		{
			core_map[i].pstate=DIRTY;
			core_map[i].npages=1;
			found=i;
			break;
		}
	}
	spinlock_release(&stealmem_lock);

	if (found==-1) {
		return 0;
	}
	//pa= freeaddr + (found * PAGE_SIZE);
	pa=core_map[found].paddr;

	return pa;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
	spinlock_acquire(&stealmem_lock);
	//spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);
	//spinlock_release(&stealmem_lock);
	return addr;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(int npages)
{
	paddr_t pa;
	if(vm_bootstrapped==0)
		pa = getppages(npages);
	else
	{
		spinlock_acquire(&stealmem_lock);
		int found=-1,start=-1;
		for(int i=0;i<last_index;i++)
		{
			if(core_map[i].pstate==FREE)
			{
				if(start==-1)
					start=i;
				if(i-start==npages-1)
				{
					for(int j=start;j<=i;j++)
					{
					core_map[start].pstate=FIXED;
					core_map[start].npages=npages;
					}
					found=i;
					break;
				}
			}
			else if(start!=-1)
			{
				start=-1;
			}
		}
		spinlock_release(&stealmem_lock);

		if (found==-1) {
			return 0;
		}
		pa= core_map[start].paddr;
	}
	if (pa==0) {
		return 0;
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_page(vaddr_t addr)
{
	for(int i=0;i<last_index;i++)
	{
		if((vaddr_t)(i*PAGE_SIZE)==addr)
		{
			//for(int j=i;j<i+core_map[i].npages;j++)
			{
				core_map[i].pstate=FREE;
			}
			break;
		}
	}
}

void
free_kpages(vaddr_t addr)
{
	for(int i=0;i<last_index;i++)
	{
		if((freeaddr+(i*PAGE_SIZE))==addr)
		{
			for(int j=i;j<i+core_map[i].npages;j++)
			{
				core_map[j].pstate=FREE;
			}
			break;
		}
	}
}

void
vm_tlbshootdown_all(void)
{
	panic("dumbvm tried to do tlb shootdown?!\n");
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
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
}*/

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		panic("dumbvm: got VM_FAULT_READONLY\n");
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

	as = curthread->t_addrspace;
	if (as == NULL) {
		/*
		 * No address space set up. This is probably a kernel
		 * fault early in boot. Return EFAULT so as to panic
		 * instead of getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->as_vbase1 != 0);
	KASSERT(as->as_pbase1 != 0);
	KASSERT(as->as_npages1 != 0);
	KASSERT(as->as_vbase2 != 0);
	KASSERT(as->as_pbase2 != 0);
	KASSERT(as->as_npages2 != 0);
	KASSERT(as->as_stackpbase != 0);
	KASSERT((as->as_vbase1 & PAGE_FRAME) == as->as_vbase1);
	KASSERT((as->as_pbase1 & PAGE_FRAME) == as->as_pbase1);
	KASSERT((as->as_vbase2 & PAGE_FRAME) == as->as_vbase2);
	KASSERT((as->as_pbase2 & PAGE_FRAME) == as->as_pbase2);
	KASSERT((as->as_stackpbase & PAGE_FRAME) == as->as_stackpbase);

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stackbase = USERSTACK - DUMBVM_STACKPAGES * PAGE_SIZE;
	stacktop = USERSTACK;
	struct PTE *temp=as->pgdir;
	while(temp!=NULL){
		if(faultaddress>=temp->vaddr && faultaddress<temp->vaddr+PAGE_SIZE)
			{
				paddr=faultaddress-temp->vaddr + temp->paddr;
				break;
			}
			temp=temp->next;
	}
	if(paddr==0)
		return EFAULT;
	/*int ind=getfirst10(faultaddress);
	if(as->pgdir[ind].pg_table==NULL){
		as->pgdir[ind].pg_table=(vaddr_t *)kmalloc(1024*sizeof(vaddr_t));
		paddr=KVADDR_TO_PADDR(faultaddress);
		as->pgdir[ind].pg_table[getsecond10(faultaddress)]=paddr;
	}
	else{
		paddr=as->pgdir[ind].pg_table[getsecond10(faultaddress)];
		paddr=(faultaddress-PADDR_TO_KVADDR(paddr))+paddr;
	}

*/
	/*if (faultaddress >= vbase1 && faultaddress < vtop1) {
		paddr = (faultaddress - vbase1) + as->as_pbase1;
	}
	else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		paddr = (faultaddress - vbase2) + as->as_pbase2;
	}
	else if (faultaddress >= stackbase && faultaddress < stacktop) {
		paddr = (faultaddress - stackbase) + as->as_stackpbase;
	}
	else {
		return EFAULT;
	}*/

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		splx(spl);
		return 0;
	}

	kprintf("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	splx(spl);
	return EFAULT;
}
