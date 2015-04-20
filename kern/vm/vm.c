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
#include <clock.h>
#include<uio.h>
#include <vnode.h>
#include<vfs.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/fcntl.h>
#include <bitmap.h>

#define DUMBVM_STACKPAGES    12

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock tlb_lock = SPINLOCK_INITIALIZER;
static struct lock *coremap_lock;

static struct vnode *swap_vnode;
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
static void getswapstats(){
	struct stat st;
	char *s=kstrdup("lhd0raw:");
	//struct coremap_page *core_map_file;
	int err=vfs_open(s,O_RDWR,0,&swap_vnode);
	if(err!=0)
			kprintf("VFS_ERROR:%d!",err);
	VOP_STAT(swap_vnode, &st);
	size_t total_swap=st.st_size/PAGE_SIZE;
	kprintf("\nSWAP MEM: %lu bytes, %d pages\n",(unsigned long)st.st_size,total_swap);
	swap_map=bitmap_create(last_index*2);
	if(swap_map==NULL)
		panic("SMAP IS NULL");
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
	//last_index=getpageindex(lastaddr);
	last_index=total_page_num;
	for(int i=0;i<total_page_num;i++)
	{
		core_map[i].paddr= (PAGE_SIZE*i) +freeaddr;
		core_map[i].vaddr=PADDR_TO_KVADDR(core_map[i].paddr);
		//kprintf("%x\t",core_map[i].paddr);
		//kprintf("%x\t",core_map[i].vaddr);
		core_map[i].pstate=FREE;
	}
	getswapstats();
	vm_bootstrapped=1;
	//kprintf("%d,%d,%d",getpageindex(0),getpageindex(freeaddr),getpageindex(lastaddr));
}

static void access_swap(paddr_t pa, vaddr_t sa, enum uio_rw mode){
	//kprintf("\naccess_entered\n");
	int result;
	struct iovec iov;
	struct uio ku;
	vaddr_t va=PADDR_TO_KVADDR(pa);
	/*
	bitmap_alloc(swap_map, &index);
	kprintf("\nAllocated Map index: %x",index);
	bitmap_alloc(swap_map, &index);
	kprintf("\nAllocated Map index: %x",index);
*/
	uio_kinit(&iov, &ku, (char *)va, PAGE_SIZE, sa, mode);
		/*iov.iov_ubase = (void *)s;
		iov.iov_len = sizeof(s);		 // length of the memory space
		ku.uio_iov = &iov;
		ku.uio_iovcnt = 1;
		ku.uio_resid = sizeof(s);          // amount to read from the file
		ku.uio_offset = 0;
		ku.uio_segflg = UIO_USERSPACE;
		ku.uio_rw = UIO_WRITE;
		ku.uio_space = curthread->t_addrspace;*/
	if(mode==UIO_READ)
		result=VOP_READ(swap_vnode,&ku);
	else
		result=VOP_WRITE(swap_vnode, &ku);
	if (result) {
//		kprintf("VOP_ops ERROR:%d",result);
	}

	//kprintf("\nDONE TO DISK!");
}
static void swapin(paddr_t pa, vaddr_t sa){
	//kprintf("\nswapin");
	lock_acquire(coremap_lock);
	access_swap(pa, sa, UIO_READ);
	lock_release(coremap_lock);
}
static void swapout(paddr_t pa, vaddr_t sa){
	lock_acquire(coremap_lock);
	//kprintf("\nswapout");
	access_swap(pa, sa, UIO_WRITE);
	lock_release(coremap_lock);
}


int count_free()
{
	int cnt=0;
	for(int i=0;i<last_index;i++)
		{
			if(core_map[i].pstate==FREE)
			{
				cnt++;
			}
		}
	return cnt;
}
/*static int choose_victim(int start){
	int victim_ind=-1;
	time_t aftersecs, secs;
	uint32_t afternsecs, nsecs,nh=0;
	gettime(&aftersecs, &afternsecs);

	for(int i=start;i<last_index;i++)
	{
		getinterval(core_map[i].beforesecs, core_map[i].beforensecs,
					aftersecs, afternsecs,
					&secs, &nsecs);
		nsecs=( secs*1000 ) + (nsecs/1000);
		//kprintf("\n%lu,%lu sec",(unsigned long)nh,(unsigned long)nsecs);

			if((unsigned long)nh==0 || (unsigned long)nh<(unsigned long)nsecs)
			{
				nh=nsecs;
				victim_ind=i;
			}

	}
	//kprintf("\n VICTIM IS %d",victim_ind);
	return victim_ind;
}*/
static int get_ind_coremap(paddr_t paddr)
{
	int i;
	for(i=0;i<last_index;i++)
			if(core_map[i].paddr==paddr)
				return i;
	return -1;
}
static struct PTE *choose_victim()
{
	int i;
	time_t aftersecs, secs;
	uint32_t afternsecs, nsecs,nh=0;
	struct addrspace* as=curthread->parent->t_addrspace;
	struct PTE *pages, *victim_pg=NULL;
	gettime(&aftersecs, &afternsecs);

	if(as==NULL)
		as=curthread->t_addrspace;

	if(as->heap!=NULL)
	{
	pages=as->heap->pages;
		while(pages!=NULL)
		{
			i=get_ind_coremap(pages->paddr);
			getinterval(core_map[i].beforesecs, core_map[i].beforensecs,
								aftersecs, afternsecs,
								&secs, &nsecs);
			nsecs=( secs*1000 ) + (nsecs/1000);
			kprintf("\nH:%lu,%lu",(unsigned long)nh,(unsigned long)nsecs);
			if((unsigned long)nh==0 || (unsigned long)nh<(unsigned long)nsecs)
			{
				nh=nsecs;
				victim_pg=pages;
			}
			pages=pages->next;
		}
	}
	pages=as->stack->pages;
		while(pages!=NULL)
		{
			i=get_ind_coremap(pages->paddr);
			getinterval(core_map[i].beforesecs, core_map[i].beforensecs,
								aftersecs, afternsecs,
								&secs, &nsecs);
			nsecs=( secs*1000 ) + (nsecs/1000);
			kprintf("\nS:%lu,%lu",(unsigned long)nh,(unsigned long)nsecs);
			if((unsigned long)nh==0 || (unsigned long)nh<(unsigned long)nsecs)
			{
				nh=nsecs;
				victim_pg=pages;
			}
			pages=pages->next;
		}/*
		while(as!=NULL)
		{
		pages=as->pages;
					while(pages!=NULL)
					{
						i=get_ind_coremap(pages->paddr);
						getinterval(core_map[i].beforesecs, core_map[i].beforensecs,
											aftersecs, afternsecs,
											&secs, &nsecs);
						nsecs=( secs*1000 ) + (nsecs/1000);
						kprintf("\nS:%lu,%lu",(unsigned long)nh,(unsigned long)nsecs);
						if((unsigned long)nh==0 || (unsigned long)nh<(unsigned long)nsecs)
						{
							nh=nsecs;
							victim_pg=pages;
						}
						pages=pages->next;
					}
			as=as->next;
		}*/
	KASSERT(victim_pg!=NULL);
	return victim_pg;
}
/*static struct PTE* find_addr(struct PTE *temp, paddr_t pa){

	while(temp!=NULL)
	{
	//	kprintf("\n%x,%x",pa,temp->paddr);
		if(temp->paddr==pa){
			temp->swapped=1;
			return temp;
		}
		temp=temp->next;
	}
	return NULL;
}

static struct PTE *get_victim_page(int vind){
	struct PTE *victim_pg=NULL;
	struct thread *threads=curthread;
	struct addrspace *reg,*stack,*heap;
	int it=0;
	while(it<2)
	{
		reg=threads->t_addrspace;
		if(reg==NULL){
			break;
		}
		stack=reg->stack;
		heap=reg->heap;
		//kprintf("\nSTART");
		while(reg!=NULL){
			victim_pg=find_addr(reg->pages,core_map[vind].paddr);
			if(victim_pg!=NULL)
				break;
			reg=reg->next;
		}
		//kprintf("\nSTACK");
		if(victim_pg==NULL && stack!=NULL)
			victim_pg=find_addr(stack->pages,core_map[vind].paddr);
		//kprintf("\nHEAP");
		if(victim_pg==NULL && heap!=NULL)
			victim_pg=find_addr(heap->pages,core_map[vind].paddr);
		if(victim_pg==NULL)
			threads=threads->parent;
		it++;
	}
	KASSERT(victim_pg!=NULL);
	return victim_pg;
}*/
static int make_page_available(int npages){
	if(npages>1)
		panic("NOO PLS");
	struct PTE *victim_pg=choose_victim();
	vaddr_t sa=victim_pg->saddr;
	int vind=get_ind_coremap(victim_pg->paddr);
	//kprintf("VIND=%d",vind);
	/*if(last_index-vind<npages)
		vind-=last_index-vind;
*/
	for(int i=vind;i<vind+npages;i++)
	{
		core_map[i].pstate=DIRTY;
		core_map[i].npages=npages;
		time_t beforesecs=0;
		uint32_t beforensecs=0;
		gettime(&beforesecs, &beforensecs);
		core_map[i].beforesecs=beforesecs;
		core_map[i].beforensecs=beforensecs;
		swapout(core_map[i].paddr, sa);
		vm_tlbshootdown(victim_pg->vaddr);
	}
	if(core_map[vind].vaddr<=USERSPACETOP){
		kprintf("%x,%x",core_map[vind].vaddr,USERSPACETOP);
		panic("POCHE");
	}
	return core_map[vind].paddr;
}
	/*
static int flush_page(){
		core_map[victim_ind].pstate=DIRTY;
		core_map[victim_ind].npages=1;
		gettime(&core_map[victim_ind].beforesecs, &core_map[victim_ind].beforensecs);

		found=victim_ind;
		//flush_to_disk(curthread->t_addrspace);
		//kprintf("\nFLUSHED");
		//get_from_disk(curthread->t_addrspace->pid);

}*/

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
			gettime(&core_map[i].beforesecs, &core_map[i].beforensecs);
			found=i;
			break;
		}
	}
	spinlock_release(&stealmem_lock);
	if (found==-1) {
				return make_page_available(1);
		}

	//pa= freeaddr + (found * PAGE_SIZE);
	pa=core_map[found].paddr;
	//kprintf("ALLOC:1 pages");
	bzero((void *)PADDR_TO_KVADDR(pa), PAGE_SIZE);
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
					core_map[j].pstate=DIRTY;
					core_map[j].npages=npages;
					gettime(&core_map[j].beforesecs, &core_map[j].beforensecs);
		//			bzero((void *)PADDR_TO_KVADDR(core_map[j].paddr), PAGE_SIZE);
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
			if(npages>1)
				panic("AYAYAOO");
			return make_page_available(npages);
		}
		pa= core_map[start].paddr;
	}
	if (pa==0) {
		return 0;
	}
	//kprintf("ALLOC:%d pages",npages);
	return PADDR_TO_KVADDR(pa);
}

void
free_page(paddr_t addr)
{
	spinlock_acquire(&stealmem_lock);
	for(int i=0;i<last_index;i++)
	{
		if(core_map[i].paddr==addr)
		{
			core_map[i].pstate=FREE;
			break;
		}
	}
	//kprintf("DALLOC:1 pages");
	spinlock_release(&stealmem_lock);
}

void
free_kpages(vaddr_t addr)
{
	spinlock_acquire(&stealmem_lock);
	for(int i=0;i<last_index;i++)
	{
		if(core_map[i].vaddr==addr)
		{
			for(int j=i;j<i+core_map[i].npages;j++)
			{
				core_map[j].pstate=FREE;
			}
		//	kprintf("DEALLOC:%d pages",core_map[i].npages);
			break;
		}
	}
	spinlock_release(&stealmem_lock);
}

void
vm_tlbshootdown_all(void)
{
	int i, spl;
	spl=splhigh();
	for(i=0;i<NUM_TLB;i++)
		tlb_write(TLBHI_INVALID(i),TLBLO_INVALID(),i);

	splx(spl);
}

void
vm_tlbshootdown(vaddr_t va)
{
	//pa = pa | TLBLO_DIRTY | TLBLO_VALID;
	uint32_t ehi, elo;
	int spl=splhigh(),i;
	//i=tlb_probe(va & PAGE_FRAME,0);
	/*if(va==0x40000)
	{
//	kprintf("\nDeleting TLB entry %d",i);*/
	//if(i==-1)
	{

	for(i=0;i<NUM_TLB;i++)
	{
		tlb_read(&ehi, &elo, i);
		if(ehi==va)
			tlb_write(TLBHI_INVALID(i),TLBLO_INVALID(),i);

//		kprintf("\n%x?,%x,%x",va,ehi,elo);
	}
	//panic("AYA");
	}
	//tlb_write(TLBHI_INVALID(i),TLBLO_INVALID(),i);
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
}*/

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	//vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr=0;
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
	/*KASSERT(as->as_vbase1 != 0);
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
	stacktop = USERSTACK;*/
	struct addrspace *rtemp=as;
	struct PTE *temp,*page=NULL;
	int found=0;
	while(rtemp!=NULL)
	{
		temp=rtemp->pages;
		while(temp!=NULL){
			if(faultaddress>=temp->vaddr && faultaddress<temp->vaddr+PAGE_SIZE)
			{
				paddr=faultaddress-temp->vaddr + temp->paddr;
				page=temp;
				found=1;
				break;
			}
			temp=temp->next;
		}
		if(found==1)
			break;
		rtemp=rtemp->next;
	}
	if(found==0 && as->stack!=NULL)
		{
			struct PTE *temp=as->stack->pages;
			while(temp!=NULL){
				if(faultaddress>=temp->vaddr && faultaddress<temp->vaddr+PAGE_SIZE)
					{
						paddr=faultaddress-temp->vaddr + temp->paddr;
						page=temp;
						found=1;
						break;
					}
					temp=temp->next;
			}
		}
	if(found==0 && as->heap!=NULL)
		{
			struct PTE *temp=as->heap->pages;
			while(temp!=NULL){
				if(faultaddress>=temp->vaddr && faultaddress<temp->vaddr+PAGE_SIZE)
					{
						paddr=faultaddress-temp->vaddr + temp->paddr;
						page=temp;
						found=1;
						break;
					}
					temp=temp->next;
			}
		}
	if(paddr==0)
	{
		paddr=alloc_page();
		//return EFAULT;
	}
	else //if(page!=NULL)
	{
		if(page->swapped==1)
		{
			swapin(paddr,page->saddr);
			page->swapped=0;
		}
	}
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
//	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();
	spinlock_acquire(&tlb_lock);
	i=tlb_probe(faultaddress & PAGE_FRAME,0);
	if(i!=-1){
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		spinlock_release(&tlb_lock);
		splx(spl);
		return 0;
	}
	for (i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		ehi = faultaddress;
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
		DEBUG(DB_VM, "dumbvm: 0x%x -> 0x%x\n", faultaddress, paddr);
		tlb_write(ehi, elo, i);
		spinlock_release(&tlb_lock);
		splx(spl);
		return 0;
	}
	ehi = faultaddress;
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	tlb_random(ehi,elo);
	//panic("dumbvm: Ran out of TLB entries - cannot handle page fault\n");
	spinlock_release(&tlb_lock);
	splx(spl);
	return 0;
}
