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
	int res;
	if(options!=909 && pid==curthread->process_id)
			return ECHILD;
	if(options!=909 && curthread->parent!=NULL && pid==curthread->parent->process_id)
			return ECHILD;
	if(pid<PID_MIN || pid>PID_MAX)
		return ESRCH;
	if(status==NULL)
		return EFAULT;
	size_t stoplen;
	if(options!=909)
	{
		res=copycheck2((const_userptr_t) status, sizeof(int), &stoplen);
		if(res)
			return EFAULT;

		if(options!=0)
			return EINVAL;

		res=copyout((const void *)&curthread->exit_code,(userptr_t)status,sizeof(int));
		if(res)
			return EFAULT;
	}
	int i,excode=0;
	struct thread *wthread=NULL;
	for(i=0;i<pcount;i++)
		if(plist[i]->pid==pid)
		{
			wthread=plist[i]->tptr;
			break;
		}
	if(i==pcount)
		return ESRCH;

	if(plist[i]->exitcode == -999)
		P(plist[i]->esem);

	excode=plist[i]->exitcode;

	if(wthread==NULL && i>=pcount)
		return ESRCH;

	if(options!=909)
	{
		res=copyout((const void *)&excode,(userptr_t)status,sizeof(int));
		if(res)
			return EFAULT;
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
	struct trapframe *child_tf=kmalloc(sizeof(struct trapframe));
	if(pcount==PID_MAX)
		return ENPROC;
	if (child_tf==NULL)
		return ENOMEM;
	*child_tf=*tf;
	struct thread* child_thread;
	struct addrspace *child_addrspace;
	int result=as_copy(curthread->t_addrspace,&child_addrspace);
	if(result)
		return ENOMEM;
	if (child_addrspace==NULL)
		return ENOMEM;

	result = thread_fork("sys_fork", (void *) entrypoint, child_tf, (unsigned long int)child_addrspace, &child_thread);
	if(result)
		return result;

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

	pname=kmalloc(sizeof(char)*PATH_MAX);

	result= copyinstr((const_userptr_t) program, pname, PATH_MAX, &len);

	if (result)
		return result;

	if(len < 2 || len >PATH_MAX)
		return EINVAL;

	argv=kmalloc(sizeof(char**));
	result= copyin((const_userptr_t) uargs, argv, sizeof(argv));

	if (result)
		return result;

	int i=0;

	while(uargs[i]!=NULL){
			argv[i] = kmalloc(sizeof(uargs[i]));
			result = copyinstr((const_userptr_t) uargs[i],argv[i], PATH_MAX, &len);
			if(len>ARG_MAX)
				return E2BIG;
			if (result)
				return result;
			i++;
	}

	argc=i;

	/* Open the file. */
	result = vfs_open(pname, O_RDONLY, 0, &v);
	if (result) {
		return result;
	}

	/* Create a new address space. */
	curthread->t_addrspace = as_create();
	if (curthread->t_addrspace==NULL) {
		vfs_close(v);
		return ENOMEM;
	}

	/* Activate it. */
	as_activate(curthread->t_addrspace);

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* thread_exit destroys curthread->t_addrspace */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(curthread->t_addrspace, &stackptr);
	if (result) {

		/* thread_exit destroys curthread->t_addrspace */
		return result;
	}

	int olen;
	i=0;
	while(i<argc)
	{
		//kprintf("%s\n",argv[i++]);
		len=strlen(argv[i])+1;
		olen=len;
		if(len%4!=0)
			len=len+4-(len%4);

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

		if(res)
			return EFAULT;

		argv[i]=(char *)stackptr;

		i++;
	}

	//if(argv[i]==NULL){
		stackptr-=4*sizeof(char);
	//}

	for(i=argc-1;i>=0;i--)
	{
		stackptr-=sizeof(char*);
		int res=copyout((const void *)(argv+i),(userptr_t)stackptr,sizeof(char*));
		if(res)
			return EFAULT;
	}

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
	curthread->exit_code=_MKWAIT_EXIT(code);
	for(i=0;i<pcount;i++)
		if(plist[i]->pid==curthread->process_id)
		{
				plist[i]->exitcode=curthread->exit_code;
				break;
		}

	V(plist[i]->esem);
	thread_exit();
	return 0;
}
