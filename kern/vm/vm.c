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

//static struct spinlock tlb_lock = SPINLOCK_INITIALIZER;
static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;

static struct vnode *swap_vnode;

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
	return i;
}
static paddr_t get_addr_by_ind(int ind){
	return (PAGE_SIZE*ind)+freeaddr;
}
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

void tlb_shootbyind(int ind){
	uint32_t elo, ehi;
	paddr_t pa;
	int cind;
	tlb_read(&ehi, &elo, ind);
	if(elo & TLBLO_VALID){
		pa=elo & TLBLO_PPAGE;
		cind=get_ind_coremap(pa);
		KASSERT(cind>=0);
		core_map[cind].tlbind=-1;
		core_map[cind].cpuid=-1;
	}
	tlb_write(TLBHI_INVALID(ind), TLBLO_INVALID(), ind);
}

void tlb_shootbyvaddr(vaddr_t vaddr){
	uint32_t elo, ehi;
	KASSERT(vaddr<MIPS_KSEG0);
	spinlock_acquire(&coremap_lock);
	int i=tlb_probe(vaddr & PAGE_FRAME, 0);
	if(i!=-1)
	{
		tlb_read(&ehi, &elo, i);
		KASSERT(elo & TLBLO_VALID); //not empty
		tlb_shootbyind(i);
	}
	spinlock_release(&coremap_lock);
}

static void tlb_wait_and_shoot(){
	wchan_lock(tlb_wchan);
	spinlock_release(&coremap_lock);
	wchan_sleep(tlb_wchan);
	spinlock_acquire(&coremap_lock);
}

static void get_swap_ready(){
	struct stat st;
	char *s=kstrdup("lhd0raw:");
	int err=vfs_open(s,O_RDWR,0,&swap_vnode);
	if(err!=0)
			kprintf("VFS_ERROR:%d!",err);
	VOP_STAT(swap_vnode, &st);
	total_swap=st.st_size/PAGE_SIZE;
	kprintf("\nSWAP MEM: %lu bytes, %d pages\n",(unsigned long)st.st_size,total_swap);
	//total_swap=last_index*3;
	lastsa=0;
	spinlock_init(&swap_address_lock);
	/*swap_map=bitmap_create(total_swap);


	while(swap_map==NULL)
	{
		total_swap-=last_index;
		swap_map=bitmap_create(total_swap);
	}*/
	//panic("SMAP IS NULL");
}
static int vm_bootstrapped=0;

void
vm_bootstrap(void)
{
	paddr_t first_addr;
	ram_getsize(&first_addr, &lastaddr);
	tlb_wchan=wchan_create("TLB_WCHAN");
	page_wchan=wchan_create("PG_WCHAN");
	biglock_paging=lock_create("biglock_paging");
	get_swap_ready();
	int total_page_num = (lastaddr-first_addr) / PAGE_SIZE;
	/* pages should be a kernel virtual address !!  */
	core_map = (struct coremap_page*)PADDR_TO_KVADDR(first_addr);
	freeaddr = first_addr + (total_page_num * sizeof(struct coremap_page));
	freeaddr=ROUNDDOWN(freeaddr);
	KASSERT((freeaddr & PAGE_FRAME) == freeaddr);
	kprintf("\n%d,%x,%x,%d\n",ROUNDDOWN(lastaddr-freeaddr) / PAGE_SIZE,freeaddr,lastaddr,total_page_num);

	last_index=0;
	for(int i=0;i<total_page_num;i++)
	{
		if( ((PAGE_SIZE*i) + freeaddr) >=lastaddr){
			last_index=i;
			break;
		}
		//core_map[i].paddr= (PAGE_SIZE*i) +freeaddr;
		//kprintf("%x\t",core_map[i].paddr);
		core_map[i].pstate=FREE;
		core_map[i].page_ptr=NULL;
		core_map[i].busy=0;
		core_map[i].cpuid=-1;
		core_map[i].tlbind=-1;
	}
	if(last_index==0)
		last_index=total_page_num;
	kprintf("%d,correct?%x,%x",last_index,lastaddr, (unsigned int)get_addr_by_ind(last_index-1));

	evict_index=0;
	vm_bootstrapped=1;
	//kprintf("%d,%d,%d",getpageindex(0),getpageindex(freeaddr),getpageindex(lastaddr));
}

static void access_swap(paddr_t pa, vaddr_t sa, enum uio_rw mode){

	int result;
	struct iovec iov;
	struct uio ku;
	vaddr_t va=PADDR_TO_KVADDR(pa);

	uio_kinit(&iov, &ku, (char *)va, PAGE_SIZE, sa, mode);

	if(mode==UIO_READ)
		result=VOP_READ(swap_vnode,&ku);
	else
		result=VOP_WRITE(swap_vnode, &ku);
	if (result) {
		panic("VOP_ops ERROR:%d",result);
	}
}
void swapin(paddr_t pa, vaddr_t sa){
	access_swap(pa, sa, UIO_READ);
	//kprintf("\nSWAPIN DONE: %x, %x", (unsigned int)pa, (unsigned int)sa);
}
void swapout(paddr_t pa, vaddr_t sa){
	access_swap(pa, sa, UIO_WRITE);
	//kprintf("\nSWAPOUT DONE: %x, %x", (unsigned int)pa, (unsigned int)sa);
}

int count_free()
{
	int cnt=0;
	for(int i=0;i<last_index;i++)
		{
			if(core_map[i].pstate==FREE && core_map[i].busy==0)
			{
				cnt++;
			}
		}
	return cnt;
}

/*Latest choose_victim
 *
 * static int choose_victim(){
	int victim_ind=-1;
	time_t aftersecs, secs;
	uint32_t afternsecs, nsecs,nh=0;
	gettime(&aftersecs, &afternsecs);

	for(int i=1;i<last_index;i++)
	{
		//if(!(core_map[i].paddr>=freeaddr && core_map[i].paddr<lastaddr))
			//continue;
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
	KASSERT(victim_ind>=0);
	//kprintf("\n VICTIM IS %d",victim_ind);
	return victim_ind;
}*/

static int choose_victim(){
	int victim_ind,rd;
	while(1)
	{
		rd=random();
	  victim_ind=rd%last_index;
	  if((core_map[victim_ind].pstate==DIRTY || core_map[victim_ind].pstate==CLEAN) && core_map[victim_ind].busy==0)
	  {
		  return victim_ind;
	  }
	}
	return -1;
}

static int make_page_available(int npages,int kernel){
	if(npages>1) // Not necessary so far
		panic("NOO, UNIMP");

	vaddr_t oldva;
	evict_index=choose_victim();
	KASSERT(evict_index>=0);
	struct PTE *victim_pg=core_map[evict_index].page_ptr;
	KASSERT(core_map[evict_index].pstate==DIRTY || core_map[evict_index].pstate==CLEAN);
	KASSERT(core_map[evict_index].busy!=1);
	KASSERT(victim_pg!=NULL);

	core_map[evict_index].busy=1;
	core_map[evict_index].npages=npages;


	vaddr_t sa=victim_pg->saddr;
	oldva=victim_pg->vaddr;

	if(core_map[evict_index].tlbind>=0){
		if(core_map[evict_index].cpuid!=curcpu->c_number){
			struct tlbshootdown ts;
			ts.core_map_ind=evict_index;
			ts.tlb_ind=core_map[evict_index].tlbind;
			ipi_tlbshootdown(core_map[evict_index].cpuid, &ts);
			while((int)core_map[evict_index].tlbind!=-1){
				tlb_wait_and_shoot();
			}
		}
		else
		{
			tlb_shootbyind(core_map[evict_index].tlbind);
		}
	}
	core_map[evict_index].cpuid=-1;
	core_map[evict_index].tlbind=-1;

	spinlock_release(&coremap_lock);
	// Evict


	if(core_map[evict_index].pstate==DIRTY)
	{
		swapout(get_addr_by_ind(evict_index) & PAGE_FRAME, sa);
		victim_pg->swapped=1;
	}
	spinlock_acquire(&coremap_lock);
	if(kernel==1)
		core_map[evict_index].pstate=FIXED;
	else
		core_map[evict_index].pstate=CLEAN;

	core_map[evict_index].page_ptr=NULL;
	wchan_wakeall(page_wchan);

	if(PADDR_TO_KVADDR(get_addr_by_ind(evict_index))<=USERSPACETOP){
		panic("WENT PAST THE USER STACK!");
	}
	return evict_index;
}

/* Allocate/free some virtual pages */
paddr_t alloc_page(struct PTE *pg)
{
	KASSERT(pg!=NULL);
	KASSERT(curthread!=NULL);
	paddr_t pa;
	int found=-1;
	if(curthread!=NULL && !curthread->t_in_interrupt){
		lock_acquire(biglock_paging);

	}
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
	core_map[found].page_ptr=pg;
	pa=get_addr_by_ind(found);
	bzero((void *)PADDR_TO_KVADDR(get_addr_by_ind(found)), PAGE_SIZE);
	spinlock_release(&coremap_lock);
	if(curthread!=NULL && !curthread->t_in_interrupt)
		lock_release(biglock_paging);

	return pa;
}

static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;
	spinlock_acquire(&stealmem_lock);
	addr = ram_stealmem(npages);
	spinlock_release(&stealmem_lock);
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
		if(curthread!=NULL && !curthread->t_in_interrupt)
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
		pa=get_addr_by_ind(start);
		spinlock_release(&coremap_lock);
		if(curthread!=NULL && !curthread->t_in_interrupt)
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
		if(core_map[j].tlbind>=0){
			tlb_shootbyind(core_map[i].tlbind);
			core_map[j].cpuid=-1;
			core_map[j].tlbind=-1;
		}
		core_map[j].pstate=FREE;
		core_map[j].busy=0;
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
	int i;
	spinlock_acquire(&coremap_lock);
	for(i=0;i<NUM_TLB;i++)
			tlb_write(TLBHI_INVALID(i),TLBLO_INVALID(),i);
	wchan_wakeall(tlb_wchan);
	spinlock_release(&coremap_lock);
}


void
vm_tlbshootdown(struct tlbshootdown *ts)
{
	spinlock_acquire(&coremap_lock);
	int cind=ts->core_map_ind;
	if(core_map[cind].tlbind==ts->tlb_ind && core_map[cind].cpuid==curcpu->c_number){
		tlb_shootbyind(ts->tlb_ind);
		core_map[cind].tlbind=-1;
	}
	wchan_wakeall(tlb_wchan);
	spinlock_release(&coremap_lock);
}

static void insert_into_tlb(vaddr_t vaddr, paddr_t paddr, int write_or_not){
	uint32_t elo, ehi;
	int i, cind, tlbind;
	KASSERT(paddr>= freeaddr && paddr<lastaddr);
	spinlock_acquire(&coremap_lock);
	cind=get_ind_coremap(paddr);
	KASSERT(cind>=0);
	tlbind=tlb_probe(vaddr & PAGE_FRAME, 0);
	if(tlbind<0){
		// INSERT INTO TLB FREE SLOT
		for (i=0; i<NUM_TLB; i++) {
			tlb_read(&ehi, &elo, i);
			if (elo & TLBLO_VALID) {
				continue;
			}
			tlbind=i;
			break;
		}
	}
	ehi = vaddr & TLBHI_VPAGE;
	elo = (paddr & TLBLO_PPAGE) | TLBLO_VALID;
	if(write_or_not==1)
		elo|=TLBLO_DIRTY;

	if(tlbind<0){
		tlb_random(ehi, elo);
		tlbind=tlb_probe(vaddr & PAGE_FRAME, 0);
	}
	else
		tlb_write(ehi, elo, tlbind);

	core_map[cind].tlbind=tlbind;
	core_map[cind].cpuid=curcpu->c_number;
	core_map[cind].busy=0;

	wchan_wakeall(page_wchan);
	spinlock_release(&coremap_lock);
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	struct addrspace *as=curthread->t_addrspace;
	int i,ind;
	vaddr_t base, top,sa;
	paddr_t paddr;
	struct region *reg, *freg=NULL;
	struct PTE *pg=NULL;
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
		pg->paddr=paddr;
		pg->vaddr=faultaddress;
		page_unlock(pg);
		ind=get_ind_coremap(paddr);
		core_map[ind].page_ptr=pg;
		bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE);
		//KASSERT(is_busy(paddr));
		page_unset_busy(paddr);
		pagetable_array_set(freg->pages, (faultaddress-base)/PAGE_SIZE, pg);
	}

	if(faulttype==VM_FAULT_READ || faulttype==VM_FAULT_WRITE)
	{
		page_sneek(pg);
		paddr=pg->paddr & PAGE_FRAME;
		ind=get_ind_coremap(paddr);

		if(pg->swapped==1)
		{
			sa=pg->saddr;
			page_unlock(pg);
			paddr=alloc_page(pg);
			KASSERT(paddr!=0);

			lock_acquire(biglock_paging);
			swapin(paddr, sa);
			pg->swapped=0;
			page_lock(pg);
			lock_release(biglock_paging);
			pg->paddr=paddr & PAGE_FRAME;
		}
		page_unlock(pg);
		/* make sure it's page-aligned */
		KASSERT((paddr & PAGE_FRAME) == paddr);
		insert_into_tlb(faultaddress, paddr, (core_map[ind].pstate==DIRTY)?1:0);
	}
	else
	{
		page_sneek(pg);
		paddr=pg->paddr;
		page_unlock(pg);
		tlb_shootbyvaddr(faultaddress);
		insert_into_tlb(faultaddress, paddr, 1);
	}

	return 0;
}
