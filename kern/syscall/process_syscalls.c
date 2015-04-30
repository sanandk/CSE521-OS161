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
#include <bitmap.h>
#include <kern/errno.h>
#include <synch.h>
#include <uio.h>
#include <syscall.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <lib.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <thread.h>
#include <threadlist.h>
#include <current.h>
#include <kern/wait.h>
#include <copyinout.h>
#include <cpu.h>
#include <vm.h>

/*
 * sys_getpid system call: get current process id
 */
int
sys___getpid(int *ret)
{

	*ret=curthread->process_id;
	return 0;
}

/*
 * sys_waitpid system call: get current process id
 */
int
sys___waitpid(int *ret,pid_t pid, int *status, int options)
{
	*ret=-1;
	int i,res, exists=-1;
	if(options!=0 && options!=909) // 909 is given by menu command
		return EINVAL;
	if(status==NULL || status==(void *)0x40000000 || status==(void *)0x80000000)
		return EFAULT;

	if(pid<PID_MIN)
		return EINVAL;
	if(pid>PID_MAX)
		return ESRCH;
	pid_t myparent=-1, hisparent=-1;
	for(i=0;i<pcount;i++)
	{
		if(plist[i]->pid==curthread->process_id)
			myparent=plist[i]->ppid;
		else if(plist[i]->pid==pid)
		{
			exists=i;
			hisparent=plist[i]->ppid;
		}
	}

	if(options!=909)	// skip for menu
	{
		if(pid==curthread->process_id){ // Dont wait for self
			return ECHILD;
		}
		if(pid==myparent){ // Dont wait for my parent
			return ECHILD;
		}
		/*if(hisparent==curthread->process_id)
		{
			return EFAULT;
		}*/
		if(myparent==hisparent){ // Dont check for menu command alone
			return EFAULT;
		}

		if(exists==-1)
			return ESRCH;
	}

	/*if(plist[exists]->tptr==NULL)
		return EFAULT;*/
	if(plist[exists]->exitcode == -999){
		P(plist[exists]->esem);
	}

	int ec=plist[exists]->exitcode;
	res=copyout((const void *)&ec,(userptr_t)status,sizeof(int));
	if(res){
		return res;
	}
	*ret=pid;
	return 0;
}


int entrypoint(void *child_tf, void* addr)
{
	struct trapframe *tf=(struct trapframe *)child_tf;
	tf->tf_a3=0;	//fork success
	tf->tf_v0=0;	//fork success
	tf->tf_epc+=4;
	curthread->t_addrspace=(struct addrspace *)addr;
	as_activate(addr);
	struct trapframe utf=*tf;
	mips_usermode(&utf);
	return 0;
}

/*
 * sys_fork system call: create a child process
 */
int
sys___fork(int *ret,struct trapframe * tf)
{
	*ret=-1;
	struct addrspace *child_addrspace;

	if(pcount==PID_MAX)
		return ENPROC;

	struct thread* child_thread;

	int result=as_copy(curthread->t_addrspace,&child_addrspace);
	if(result)
	{
		//panic("ASCOPYLA");
		return ENOMEM;
	}

	struct trapframe *child_tf=kmalloc(sizeof(struct trapframe));
		if (child_tf==NULL){
			//panic("|trapframe malloc failed}");
			return ENOMEM;
		}
		*child_tf=*tf;

	//if (child_addrspace==NULL)
		//return ENOMEM;

	result = thread_fork("sys_fork", (void *) entrypoint, child_tf, (unsigned long)child_addrspace, &child_thread);
	if(result)
	{
		//panic("fork ae pochu");
		return ENOMEM;
	}

	*ret=child_thread->process_id;
	return 0;
}

/*
 * sys_execv system call: run the program
 */
int
sys___execv(int *ret,const char *program, char **uargs)
{
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result,argc;
	char *pname, **argv;
	size_t len;

	*ret=-1;
	if(program==NULL || uargs == NULL)
		return EFAULT;
	lock_acquire(execv_lock);

	pname=(char *)kmalloc(sizeof(char)*PATH_MAX);

	result= copyinstr((const_userptr_t) program, pname, PATH_MAX, &len);
	if((void *) program== (void *)0x40000000 || (void *) uargs== (void *)0x40000000){
		lock_release(execv_lock);
		return EFAULT;
	}

	if((char *)program=='\0' || (char *)program==NULL){
		lock_release(execv_lock);
		return EINVAL;
	}

	if (result)
	{
		lock_release(execv_lock);
		return result;
	}

	if(len < 2 || len >PATH_MAX)
	{
		lock_release(execv_lock);
		return EINVAL;
	}

	argv=(char **)kmalloc(sizeof(char**));
	result= copyin((const_userptr_t) uargs, argv, sizeof(argv));

	if (result){
		lock_release(execv_lock);
		return EFAULT;
	}

	int i=0;
	/*result=copycheck2((const_userptr_t) uargs, sizeof(uargs), &len);
		if(result){
			lock_release(execv_lock);
			return EFAULT;
		}
	if(*uargs=='\0'){
				lock_release(execv_lock);
				return EINVAL;
			}*/
	while(uargs[i]!=NULL){
		  	  /*if((void *) uargs[i]== (void *)0x40000000){
		  		  lock_release(execv_lock);
		  		  return EFAULT;
		  	  }*/
			argv[i] = (char *)kmalloc(sizeof(char)*PATH_MAX);
			result = copyinstr((const_userptr_t) uargs[i],argv[i], PATH_MAX, &len);
			if(len>ARG_MAX){
				lock_release(execv_lock);
				return E2BIG;
			}
			if (result){
				lock_release(execv_lock);
				return EFAULT;
			}
			i++;
	}
	argv[i]=NULL;
	argc=i;

	/* Open the file. */
	result = vfs_open(pname, O_RDONLY, 0, &v);
	if (result) {
		lock_release(execv_lock);
		return EFAULT;
	}

	if(curthread->t_addrspace !=NULL){
		as_destroy(curthread->t_addrspace );
		curthread->t_addrspace =NULL;
	}

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		lock_release(execv_lock);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		lock_release(execv_lock);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {
		lock_release(execv_lock);
		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	int olen;
	i=0;
	//while(i<argc)
	while(argv[i]!=NULL)
	{
		//kprintf("%s\n",argv[i++]);
		len=strlen(argv[i])+1;
		olen=len;
		if(len%4!=0)
			len=len+(4-len%4);

		char *str=kmalloc(sizeof(len));
		str=kstrdup(argv[i]);	//dont need actually
		for(int j=0;j<(int)len;j++)
		{
			if(j>=olen)
				str[j]='\0';
			else
				str[j]=argv[i][j];
		}

		stackptr-=len;

		int res=copyout((const void *)str,(userptr_t)stackptr,len);

		if(res){
			lock_release(execv_lock);
			return EFAULT;
		}

		argv[i]=(char *)stackptr;

		i++;
	}

	if(argv[i]==NULL){
		stackptr-=4*sizeof(char);
	}

	for(i=argc-1;i>=0;i--)
	{
		stackptr-=sizeof(char*);
		int res=copyout((const void *)(argv+i),(userptr_t)stackptr,sizeof(char*));
		if(res){
			lock_release(execv_lock);
			return EFAULT;
		}
	}
	lock_release(execv_lock);
	//kprintf("=%d=%d=",i,argc);
	/* Warp to user mode. */
	enter_new_process(argc, (userptr_t)stackptr,
			  stackptr, entrypoint);


	return 0;
}

/*
 * sys_exit system call: exit process
 */
int
sys___exit(int code)
{
	int i;
	for(i=0;i<pcount;i++)
		if(plist[i]->pid==curthread->process_id)
		{
				curthread->exit_code=_MKWAIT_EXIT(code);
				plist[i]->exitcode=curthread->exit_code;
				vm_tlbshootdown_all();
				V(plist[i]->esem);
				break;
		}
	thread_exit();
	return 0;
}

int
sys___sbrk(int *ret, int amt)
{
	struct region *heap=regions_array_get(curthread->t_addrspace->regions,2);
	vaddr_t heap_start=curthread->t_addrspace->heap_start, heap_end=curthread->t_addrspace->heap_end;
	kprintf("\nNOP: %d",heap->as_npages);
	struct PTE *temp;
	if(amt==0){
		*ret=heap_end;
		return 0;
	}
	if(amt%4!=0){
		*ret=-1;
		kprintf("\namt not 4\n");
		return EINVAL;
	}
	if((heap_end+amt)<heap_start)
	{
		*ret=-1;
		return EINVAL;
	}
	KASSERT((heap_end+amt)>=heap_start);

	if(amt<0){
		amt*=-1; //remove negative sign
		if(amt<PAGE_SIZE){
			*ret=heap_end;
			heap_end-=amt;
		}
		else
		{
			size_t no=amt/PAGE_SIZE;
			if((int)(heap->as_npages-no)<0)
			{
				*ret=-1;
				return EINVAL;
			}
				int i=0;
				for(int j=pagetable_array_num(heap->pages);j>=0;j--)
				{
					temp=pagetable_array_get(heap->pages, j);
					page_set_busy(temp->paddr);
					free_page(temp->paddr);
					spinlock_cleanup(&temp->slock);
					page_unset_busy(temp->paddr);

					kfree(temp);

					if(++i==(int)no){
						break;
					}
				}
			heap->as_npages-=no;
			*ret=heap_end;
			heap_end-=amt;
		}
	}
	else
	{
		if((heap_end+amt)>(USERSTACK-(12*PAGE_SIZE)))
		{
			*ret=-1;
			return ENOMEM;
		}
		if(amt<PAGE_SIZE && (PAGE_SIZE- ((int)(heap_end-heap_start)%PAGE_SIZE)) > amt)
		{
			*ret=heap_end;
			heap_end+=amt;
		}
		else
		{
			int oamt=amt;
			amt-=PAGE_SIZE - ((heap_end-heap_start)%PAGE_SIZE);
			int no=amt/PAGE_SIZE;
			if(amt%PAGE_SIZE){
				no++;
			}
			if(last_index<no){
				*ret=-1;
				return ENOMEM;
			}

			int i=pagetable_array_setsize(heap->pages, heap->as_npages+no);
			KASSERT(i==0);
			for(i=heap->as_npages;i<(int)heap->as_npages+no;i++){
				pagetable_array_set(heap->pages, i, NULL);
			}

			heap->as_npages+=no;
			*ret=heap_end;
			heap_end+=oamt;
		}
	}

	curthread->t_addrspace->heap_end=heap_end;

	return 0;
}
