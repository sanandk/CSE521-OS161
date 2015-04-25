#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <array.h>
#include <addrspace.h>
#include <spl.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <mips/tlb.h>
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
#include <synch.h>
#include <cpu.h>

DEFARRAY_BYTYPE(pagetable_array, struct PTE, );
#define DUMBVM_STACKPAGES    12

static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;
static struct spinlock tlb_lock = SPINLOCK_INITIALIZER;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

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
int get_ind_coremap(paddr_t paddr)
{
	int i=(paddr-freeaddr)/PAGE_SIZE;
	if(i>last_index || i<0){
		return -1;
	}
	/*for(i=0;i<last_index;i++)
			if(core_map[i].paddr==paddr)
				return i;*/
	return i;
}
/*static struct PTE *lookup_region(vaddr_t va, struct addrspace *as, int pa){
	if(as==NULL)
		return NULL;
	struct PTE *temp=as->pages;
	while(temp!=NULL){
		if(pa==0)
			if((unsigned int)va>=(unsigned int)temp->vaddr && (unsigned int)va<(unsigned int)temp->vaddr + PAGE_SIZE)
				return temp;
		if(pa==1)
			if((unsigned int)va>=(unsigned int)temp->paddr && (unsigned int)va<(unsigned int)temp->paddr + PAGE_SIZE)
				return temp;
		temp=temp->next;
	}
	return NULL;
}
static struct PTE* find_in_as(vaddr_t va){
	struct PTE *pg=NULL;
	struct addrspace *as=curthread->t_addrspace,*temp;
	if(as==NULL)
		return NULL;
	int it=0;
	try:
	temp=as;
	while(pg==NULL && temp!=NULL){
		pg=lookup_region(va, temp,0);
		temp=temp->next;
	}
	if(pg==NULL)
		pg=lookup_region(va, as->stack,0);
	if(pg==NULL)
		pg=lookup_region(va, as->heap,0);
	if(pg==NULL && it==0){
		as=curthread->parent->t_addrspace;
		it++;
		goto try;
	}
	return pg;
}*/
/*static void delete_page(struct PTE *dpage, struct addrspace *as){
	struct PTE *temp=as->pages;
	if(temp==dpage)
	{
		as->pages=temp->next;
		kfree(dpage);
		return;
	}
	while(temp!=NULL)
	{
		if(temp->next==dpage)
		{
			temp->next=dpage->next;
			kfree(dpage);
			return;
		}
		temp=temp->next;
	}
}
static void del_in_as(vaddr_t va){
	struct PTE *pg=NULL;
	struct addrspace *as=curthread->t_addrspace,*temp;
	if(as==NULL)
		return;
	int it=0;
	try:
	temp=as;
	while(pg==NULL && temp!=NULL){
		pg=lookup_region(va, temp,0);
		if(pg!=NULL)
			delete_page(pg,temp);
		temp=temp->next;
	}
	if(pg==NULL){
		pg=lookup_region(va, as->stack,0);
		if(pg!=NULL)
			delete_page(pg,as->stack);
	}
	if(pg==NULL){
		pg=lookup_region(va, as->heap,0);
		if(pg!=NULL)
			delete_page(pg,as->heap);
	}

	if(pg==NULL && it==0){
		as=curthread->parent->t_addrspace;
		it++;
		goto try;
	}
}*/
/*static struct PTE* pfind_in_as(paddr_t va){
	struct PTE *pg=NULL;
	struct addrspace *as=curthread->t_addrspace,*temp;
	temp=as;
	if(as==NULL)
		panic("NULL");

	while(pg==NULL && temp!=NULL){
		pg=lookup_region(va, temp,1);
		temp=temp->next;
	}
	if(pg==NULL)
		pg=lookup_region(va, as->stack,1);
	if(pg==NULL)
		pg=lookup_region(va, as->heap,1);
	return pg;
}*/
static void wait_for_busypage(){
	wchan_lock(page_wchan);
	spinlock_release(&coremap_lock);
	wchan_sleep(page_wchan);
	spinlock_acquire(&coremap_lock);
}

void page_lock(struct PTE *pg){
	spinlock_acquire(&pg->slock);
}
void page_unlock(struct PTE *pg){
	spinlock_release(&pg->slock);
}
void page_set_busy(paddr_t pa){
	int i=get_ind_coremap(pa);
	KASSERT(i>=0);
	spinlock_acquire(&coremap_lock);
	while(core_map[i].busy){
		wait_for_busypage();
	}
	core_map[i].busy=1;
	spinlock_release(&coremap_lock);
}
void page_unset_busy(paddr_t pa){
	int i=get_ind_coremap(pa);
	KASSERT(i>=0);
	spinlock_acquire(&coremap_lock);
	//KASSERT(core_map[i].busy==1);
	core_map[i].busy=0;
	wchan_wakeall(page_wchan);
	spinlock_release(&coremap_lock);
}
int is_busy(paddr_t pa){
	int i=get_ind_coremap(pa);
	KASSERT(i>=0);
	return core_map[i].busy;
}
void page_sneek(struct PTE *pg){
	paddr_t beef=0xdeadbeef;
	paddr_t pa,temp=beef;
	page_lock(pg);
	while(1)
	{
		pa=pg->paddr & PAGE_FRAME;
		if(pg->swapped==1)
			pa=beef;
		if((unsigned int)pa==(unsigned int)temp)
			break;
		page_unlock(pg);
		if((unsigned int)temp!=(unsigned int)beef){
			page_unset_busy(temp);
		}
		if((unsigned int)pa==(unsigned int)beef){
			page_lock(pg);
			KASSERT(pg->swapped==1);
			break;
		}
		page_set_busy(pa);
		temp=pa;
		page_lock(pg);
	}
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
	total_swap=last_index*3;
	swap_map=bitmap_create(total_swap);
	lastsa=-1;
	//swap_map=bitmap_create(last_index+last_index+last_index+(last_index/2));
	/*swap_map=bitmap_create(2);
	vaddr_t sa;
	bitmap_alloc(swap_map, &sa);
		kprintf("\ni=%x",sa);
		bitmap_alloc(swap_map, &sa);
			kprintf("\ni=%x",sa);
			bitmap_alloc(swap_map, &sa);
				kprintf("\ni=%x",sa);
				panic("A");*/
	while(swap_map==NULL)
	{
		total_swap-=last_index;
		swap_map=bitmap_create(total_swap);
	}
	//panic("SMAP IS NULL");
}
static void tlb_wait_and_shoot(){
	wchan_lock(tlb_wchan);
	spinlock_release(&coremap_lock);
	wchan_sleep(tlb_wchan);
	spinlock_acquire(&coremap_lock);
}

void
vm_bootstrap(void)
{
	paddr_t first_addr, lastaddr;
	ram_getsize(&first_addr, &lastaddr);

	tlb_wchan=wchan_create("TLB_WCHAN");
	page_wchan=wchan_create("PG_WCHAN");
	biglock_paging=lock_create("biglock_paging");
	getswapstats();
	int total_page_num = (lastaddr-first_addr) / PAGE_SIZE;
	/* pages should be a kernel virtual address !!  */
	core_map = (struct coremap_page*)PADDR_TO_KVADDR(first_addr);
	freeaddr = first_addr + (total_page_num * sizeof(struct coremap_page));
	freeaddr=ROUNDDOWN(freeaddr);

	kprintf("\n%d,%x,%x,%d\n",ROUNDDOWN(lastaddr-freeaddr) / PAGE_SIZE,first_addr,lastaddr,total_page_num);

	last_index=0;
	for(int i=0;i<total_page_num;i++)
	{
		if( ((PAGE_SIZE*i) + freeaddr) >=lastaddr){
			last_index=i;
			break;
		}
		core_map[i].paddr= (PAGE_SIZE*i) +freeaddr;
		//kprintf("%x\t",core_map[i].paddr);
		core_map[i].pstate=FREE;
		core_map[i].page_ptr=NULL;
		core_map[i].busy=0;
		core_map[i].cpuid=0;
		core_map[i].tlbind=-1;
	}
	if(last_index==0)
		last_index=total_page_num;
	kprintf("%d,correct?%x,%x",last_index,core_map[last_index-1].paddr,lastaddr);

	evict_index=0;
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
void swapin(paddr_t pa, vaddr_t sa){
	//kprintf("\nswapin");
	access_swap(pa, sa, UIO_READ);
}
void swapout(paddr_t pa, vaddr_t sa){
	//kprintf("\nswapout");
	access_swap(pa, sa, UIO_WRITE);
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
static int choose_victim(){
	int victim_ind=-1;
	time_t aftersecs, secs;
	uint32_t afternsecs, nsecs,nh=0;
	gettime(&aftersecs, &afternsecs);

	for(int i=0;i<last_index;i++)
	{
		getinterval(core_map[i].beforesecs, core_map[i].beforensecs,
					aftersecs, afternsecs,
					&secs, &nsecs);
		nsecs=( secs*1000 ) + (nsecs/1000);
		//kprintf("\n%lu,%lu sec",(unsigned long)nh,(unsigned long)nsecs);

			if((core_map[i].pstate==DIRTY && core_map[i].busy==0) && ((unsigned long)nh==0 || (unsigned long)nh<(unsigned long)nsecs))
			{
				nh=nsecs;
				victim_ind=i;
			}
	}
	//kprintf("\n VICTIM IS %d",victim_ind);
	return victim_ind;
}

/*static struct PTE *choose_victim()
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
			if(core_map[i].pstate!=FIXED && ((unsigned long)nh==0 || (unsigned long)nh<(unsigned long)nsecs))
			{
				nh=nsecs;
				victim_pg=pages;
			}
			pages=pages->next;
		}
	}
	if(as->stack!=NULL)
	{
	pages=as->stack->pages;
		while(pages!=NULL)
		{
			i=get_ind_coremap(pages->paddr);
			getinterval(core_map[i].beforesecs, core_map[i].beforensecs,
								aftersecs, afternsecs,
								&secs, &nsecs);
			nsecs=( secs*1000 ) + (nsecs/1000);
			if(core_map[i].pstate!=FIXED && ((unsigned long)nh==0 || (unsigned long)nh<(unsigned long)nsecs))
			{
				nh=nsecs;
				victim_pg=pages;
			}
			pages=pages->next;
		}
	}
	KASSERT(victim_pg!=NULL);
	return victim_pg;
}*/
/*static int make_page_available(int npages,int kernel){
	if(npages>1)
		panic("NOO PLS");

	struct PTE *victim_pg=choose_victim();
	victim_pg->swapped=1;
	vaddr_t sa=victim_pg->saddr;
	int vind=get_ind_coremap(victim_pg->paddr);
	//kprintf("VIND=%d",vind);
	if(last_index-vind<npages)
		vind-=last_index-vind;

	for(int i=vind;i<vind+npages;i++)
	{
		if(kernel==1)
			core_map[i].pstate=FIXED;
		else
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
	//bzero((void *)PADDR_TO_KVADDR(core_map[vind].paddr), PAGE_SIZE);
	return core_map[vind].paddr;
}*/
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

/* Sequential replacing */
static int make_page_available(int npages,int kernel){
	if(npages>1) // Not necessary so far
		panic("NOO PLS");
	vaddr_t oldva;
	evict_index=choose_victim();
	KASSERT(evict_index>=0);
	struct PTE *victim_pg=core_map[evict_index].page_ptr;
	KASSERT(core_map[evict_index].pstate!=FIXED);
	KASSERT(core_map[evict_index].busy!=1);
//	KASSERT(count_free()==0);
	KASSERT(victim_pg!=NULL);

	core_map[evict_index].busy=1;
	core_map[evict_index].npages=npages;
	if(kernel==0)
		core_map[evict_index].pstate=DIRTY;
	else
		core_map[evict_index].pstate=FIXED;

	victim_pg->swapped=1;
	vaddr_t sa=victim_pg->saddr;
	oldva=victim_pg->vaddr;
	//vm_tlbshootdown(evict_index, 1);

	if(core_map[evict_index].tlbind>=0){
		if(core_map[evict_index].cpuid!=curcpu->c_number){
			struct tlbshootdown ts;
			ts.ts_addrspace=curthread->t_addrspace;
			ts.ts_vaddr=oldva;
			ipi_tlbshootdown(core_map[evict_index].cpuid, &ts);
			while((int)core_map[evict_index].cpuid!=-1){
				tlb_wait_and_shoot();
			}
		}
		else
		{
			vm_tlbshootdown(evict_index, 1);
		}
	}
	spinlock_release(&coremap_lock);
	swapout(core_map[evict_index].paddr, sa);
	spinlock_acquire(&coremap_lock);
	core_map[evict_index].busy=0;
	wchan_wakeall(page_wchan);

	if(PADDR_TO_KVADDR(core_map[evict_index].paddr)<=USERSPACETOP){
		panic("WENT PAST THE USER STACK!");
	}
	return evict_index;
}

/* Allocate/free some kernel-space virtual pages */
vaddr_t alloc_page(struct PTE *pg)
{
	KASSERT(curthread!=NULL);
	paddr_t pa;
	int found=-1;

	lock_acquire(biglock_paging);
	spinlock_acquire(&coremap_lock);

	for(int i=0;i<last_index;i++)
	{
		if(core_map[i].pstate==FREE && core_map[i].busy==0)
		{
			found=i;
			break;
		}
	}

	if (found==-1) {
		found=make_page_available(1,0);
	}
	KASSERT(found>=0);

	core_map[found].pstate=DIRTY;
	core_map[found].npages=1;
	core_map[found].busy=1;
	gettime(&core_map[found].beforesecs, &core_map[found].beforensecs);
	core_map[found].page_ptr=pg;
	pa=core_map[found].paddr;
	KASSERT(pa!=0);

	//bzero((void *)PADDR_TO_KVADDR(pa), PAGE_SIZE);
	spinlock_release(&coremap_lock);
	lock_release(biglock_paging);

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
		lock_acquire(biglock_paging);
		spinlock_acquire(&coremap_lock);
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
					core_map[j].pstate=FIXED;
					core_map[j].busy=0;
					core_map[j].page_ptr=NULL;
					core_map[j].npages=npages;
					gettime(&core_map[j].beforesecs, &core_map[j].beforensecs);
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
		if (found==-1) {
			found=make_page_available(npages,1);
			core_map[found].pstate=FIXED;
			core_map[found].npages=npages;
			core_map[found].busy=0;
			core_map[found].page_ptr=NULL;
			start=found;
		}
		pa=core_map[start].paddr;
		spinlock_release(&coremap_lock);
		lock_release(biglock_paging);
	}
	return PADDR_TO_KVADDR(pa);
}

void
free_page(paddr_t addr)
{
	int i=get_ind_coremap(addr);
	//KASSERT(i!=-1);
	//KASSERT(core_map[i].pstate!=FREE);
	//KASSERT(core_map[i].busy==1 || core_map[i].pstate==FIXED);
	spinlock_acquire(&coremap_lock);
	for(int j=i;j<i+core_map[i].npages;j++)
	{
		vm_tlbshootdown(j, 1);
		core_map[j].pstate=FREE;
		core_map[j].busy=0;
		core_map[j].cpuid=0;
		core_map[j].tlbind=-1;
		core_map[j].page_ptr=NULL;
	}
	spinlock_release(&coremap_lock);
}

void
free_kpages(vaddr_t addr)
{
	paddr_t pa=KVADDR_TO_PADDR(addr);
	free_page(pa);
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
vm_tlbshootdown(vaddr_t va, int ind_or_not)
{
	uint32_t ehi, elo;
	paddr_t pa;
	int spl=splhigh(),i;
	if(ind_or_not==1){
		i=va;
		core_map[i].tlbind=-1;
		core_map[i].cpuid=0;
	}
	else
		i=tlb_probe(va & PAGE_FRAME,0);

	if(i==-1)
		return;

	if(ind_or_not==0){
	tlb_read(&ehi, &elo, i);
	pa= elo & TLBLO_VALID; // check this
	int ind=get_ind_coremap(pa);
	KASSERT(ind>=0);
	core_map[ind].tlbind=-1;
	core_map[ind].cpuid=0;
	}

	tlb_write(TLBHI_INVALID(i),TLBLO_INVALID(),i);

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




/*static struct PTE* init_fault(vaddr_t faultaddress){
	struct addrspace *curspace=curthread->t_addrspace,*as=curspace;
	vaddr_t base,top,sa;

	//Check regions
	while(as!=NULL){
		base=as->as_vbase;
		if(as->next!=NULL)
			top=as->next->as_vbase;
		else
			top=curspace->heap_start;

		if((unsigned int)faultaddress>=(unsigned int)base && (unsigned int)faultaddress<(unsigned int)top)
				break;
		as=as->next;
	}

	struct PTE *pg=(struct PTE *)kmalloc(sizeof(struct PTE));
	pg->next=NULL;
	pg->perm=0;
	pg->vaddr=faultaddress;
	spinlock_init(&pg->slock);
	bitmap_alloc(swap_map, &sa);
	lastsa=sa;
	pg->saddr=sa*PAGE_SIZE;
	pg->swapped=0;
	pg->paddr=alloc_page(pg);
	struct PTE *ptable=as->pages;
	while(ptable->next!=NULL)
		ptable=ptable->next;
	ptable->next=pg;
	page_unset_busy(pg->paddr);
	return pg;
}*/
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace *as=curthread->t_addrspace;
	int i;
	vaddr_t base, top,sa;
	paddr_t paddr;
	struct region *reg, *freg=NULL;
	struct PTE *pg;
	for(i=0;i<(int)regions_array_num(as->regions);i++)
	{
		reg=regions_array_get(as->regions, i);
		base=reg->as_vbase;
		top=base+ (PAGE_SIZE * pagetable_array_num(reg->pages));
		if(faultaddress>=base && faultaddress<top){
			freg=reg;
			break;
		}
	}
	if(freg==NULL){
		return EFAULT;
	}

	pg=pagetable_array_get(freg->pages, (faultaddress-base)/PAGE_SIZE);
	if(pg==NULL)
	{
		page_create(&pg, &paddr);
		pg->vaddr=faultaddress;
		page_unlock(pg);
		bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
		KASSERT(is_busy(paddr));
		page_unset_busy(paddr);
		pagetable_array_set(freg->pages, (faultaddress-base)/PAGE_SIZE, pg);
	}

	page_sneek(pg);
	paddr=pg->paddr;
	if(pg->swapped==1)
	{
		sa=pg->saddr;
		page_unlock(pg);
		paddr=alloc_page(pg);
		KASSERT(paddr!=0);

		lock_acquire(biglock_paging);
		swapin(paddr, sa);
		page_lock(pg);
		lock_release(biglock_paging);
		pg->paddr=paddr & PAGE_FRAME;
	}
	page_unlock(pg);


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

	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);
	spinlock_acquire(&coremap_lock);
	/* Disable interrupts on this CPU while frobbing the TLB. */
	uint32_t ehi, elo;
	int spl = splhigh(), ti;

	int ind=get_ind_coremap(paddr);
	KASSERT(ind>=0);
	ti=tlb_probe(faultaddress, 0);

	if(ti<0){
			// INSERT INTO TLB FREE SLOT
			for (i=0; i<NUM_TLB; i++) {
				tlb_read(&ehi, &elo, i);
				if (elo & TLBLO_VALID) {
					continue;
				}
				ti=i;
				core_map[ind].tlbind=ti;
				core_map[ind].cpuid=curcpu->c_number;
				break;
			}
			if(ti<0)
			{
				ehi = faultaddress & TLBHI_VPAGE;
				elo = (paddr & TLBLO_PPAGE) | TLBLO_DIRTY | TLBLO_VALID;
				tlb_random(ehi, elo);
				ti=tlb_probe(faultaddress, 0);
				core_map[ind].tlbind=ti;
				core_map[ind].cpuid=curcpu->c_number;
				ti=-2;
			}
	}
	if(ti>=0){
	KASSERT(core_map[ind].tlbind==ti);
	KASSERT(core_map[ind].cpuid==curcpu->c_number);
	ehi = faultaddress & TLBHI_VPAGE;
	elo = (paddr & TLBLO_PPAGE) | TLBLO_DIRTY | TLBLO_VALID;
	tlb_write(ehi, elo, ti);
	tlb_read(&ehi, &elo, ti);
	}

	core_map[ind].busy=0;
	wchan_wakeall(page_wchan);
	spinlock_release(&coremap_lock);
	(void)tlb_lock;
	splx(spl);
	return 0;

}
