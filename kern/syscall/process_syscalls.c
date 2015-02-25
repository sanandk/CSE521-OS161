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
	if(pid==curthread->process_id)	//also check parent process
			return ECHILD;
	if(status==NULL)
			return EFAULT;
	if(options!=0)
			return EINVAL;
	if(pid<PID_MIN || pid>PID_MAX)
			return ESRCH;

	struct thread *wthread=NULL;

	// Look for non-exited threads
	struct threadlistnode ptr=curcpu->c_runqueue.tl_head;
			while(1)
			{
				if(ptr.tln_self == curcpu->c_runqueue.tl_tail.tln_self)
					break;
				if(ptr.tln_self->process_id==pid)
				{
					wthread=ptr.tln_self;
					break;
				}
				ptr=*(ptr.tln_next);
			}

	if(wthread==NULL)
	{
		// Look for zombies (exited threads)
		struct threadlistnode ptr=curcpu->c_zombies.tl_head;
		while(1)
		{
			if(ptr.tln_self == curcpu->c_zombies.tl_tail.tln_self)
				break;
			if(ptr.tln_self->process_id==pid)
			{
				wthread=ptr.tln_self;
				break;
			}
			ptr=*(ptr.tln_next);
		}
	}
	else
		P(wthread->exit_sem);

	if(wthread==NULL)
		return ESRCH;

	*ret=wthread->exit_code;
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

	if (child_tf==NULL)
		return ENOMEM;
	*child_tf=*tf;
	struct thread* child_thread;
	struct addrspace *child_addrspace;
	as_copy(curthread->t_addrspace,&child_addrspace);
	if (child_addrspace==NULL)
		return ENOMEM;

	int result = thread_fork("sys_fork", (void *) entrypoint, child_tf, (unsigned long int)child_addrspace, &child_thread);
	if(result)
		return result;


	*ret=child_thread->process_id;
	return 0;
}

/*
 * sys_exit system call: exit process
 */
int
sys___exit(int code)
{
	curthread->exit_code=_MKWAIT_EXIT(code);
	V(curthread->exit_sem);
	thread_exit();

	return 0;
}
