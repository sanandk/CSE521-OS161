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
#include <thread.h>
#include <kern/errno.h>
#include <synch.h>
#include <uio.h>
#include <syscall.h>
#include <lib.h>
#include <vnode.h>
#include <vfs.h>
#include <kern/fcntl.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <current.h>
#include <kern/wait.h>
#include <copyinout.h>

/*
 * sys_getpid system call: get current process id
 */


int std_open(int fd, int flags)
{
	struct vnode *v;
	char *c;
	c=kstrdup("con:");
		int i=fd,result;

		/* Open the file. */
		result = vfs_open(c, flags, 0, &v);
		if (result) {
			// error
			kfree(v);
			return EINVAL;
		}

		curthread->f_handles[i]=kmalloc(sizeof(struct file_handle));
		curthread->f_handles[i]->file_counter=1;
		curthread->f_handles[i]->file_name=kstrdup(c);
		curthread->f_handles[i]->file_offset=0;
		curthread->f_handles[i]->file_ref=v;
		curthread->f_handles[i]->op_flags=flags;
		curthread->f_handles[i]->file_lock=lock_create(c);
		return 0;
}


/*
 * sys_open system call: open file
 */
int
sys___open(int *ret, char *filename, int flags, mode_t mode)
{
	struct vnode *v;
	int i,result;
	char *fname;
	size_t len;

	/*if(filename==NULL)
	{
			*ret=-1;
			return EFAULT;
	}*/
	if(flags>66)
	{
		*ret=-1;
		return EINVAL;
	}
	//fname=filename;
	/*result = copycheck2((const_userptr_t) filename, PATH_MAX, &len);
	if (result) {
		kprintf("|copycheck2 failed|");
		*ret=-1;
		return result;
	}*/
	fname=kmalloc(sizeof(char)*PATH_MAX);
	result= copyinstr((const_userptr_t) filename, fname, PATH_MAX, &len);

		if (result)
		{
			*ret=-1;
			return result;
		}

	/* Open the file. */
	result = vfs_open(fname, flags, mode, &v);
	if (result) {
		// error
		*ret=-1;
		return EINVAL;
	}


	/* Allocate FD 	 */

	for(i=3;i<curthread->fd_count+3;i++)
	{
		if(curthread->f_handles[i]==NULL)
			break;
	}
	if(i==OPEN_MAX)
	{
		*ret=-1;
		return EMFILE;
	}

	curthread->f_handles[i]=kmalloc(sizeof(*curthread->f_handles[i]));
	if(i>curthread->fd_count)
		curthread->fd_count++;
	curthread->f_handles[i]->file_counter=1;
	curthread->f_handles[i]->file_name=kstrdup(fname);
	curthread->f_handles[i]->file_offset=0;
	curthread->f_handles[i]->op_flags=flags;
	curthread->f_handles[i]->file_ref=v;
	curthread->f_handles[i]->file_lock=lock_create(fname);
	*ret=i;

	return 0;
}

int
sys___read(int *ret, int fd, void *buf, size_t bufsize)
{

	if(fd<0 || fd>=OPEN_MAX)
	{
		*ret=-1;
		return EBADF;
	}
	void *kbuf=kmalloc(sizeof(*buf)*bufsize);
	int result;
	if(kbuf==NULL)
	{
		*ret=-1;
		return EFAULT;
	}
	size_t len;
	result = copycheck2((const_userptr_t) buf, bufsize, &len);
		if (result) {
			return result;
		}

	if(fd>curthread->fd_count+2 || curthread->f_handles[fd]==NULL)
	{
		*ret=-1;
		return EBADF;
	}
	struct file_handle *file=curthread->f_handles[fd];
	struct vnode *v=file->file_ref;
	struct iovec iov;
	struct uio u;

	if(file==NULL || file->op_flags==O_WRONLY)
	{
		*ret=-1;
		return EBADF;
	}


	lock_acquire(file->file_lock);

	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = bufsize;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = bufsize;          // amount to read from the file
	u.uio_offset = file->file_offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curthread->t_addrspace;

	result = VOP_READ(v, &u);
	if (result) {
		lock_release(file->file_lock);
		*ret=-1;
		return result;
	}

	file->file_offset=u.uio_offset;
	*ret=bufsize-u.uio_resid;
	lock_release(file->file_lock);

	return 0;
}

int
sys___write(int *ret, int fd, void *buf, size_t bufsize)
{
	if(fd<0 || fd>=OPEN_MAX)
	{
		*ret=-1;
		return EBADF;
	}
	if( fd>curthread->fd_count+2 || curthread->f_handles[fd]==NULL)
	{
			*ret=-1;
			return EBADF;
	}

	void *kbuf=kmalloc(sizeof(*buf)*bufsize);
	if(kbuf==NULL)
	{
		*ret=-1;
		return EINVAL;
	}
	int result;
	size_t len;
	result = copycheck2((const_userptr_t) buf, bufsize, &len);
	if (result) {
		return result;
	}


	struct file_handle *file=curthread->f_handles[fd];
	struct vnode *v=file->file_ref;
	struct iovec iov;
	struct uio u;

	if(file==NULL || file->op_flags==O_RDONLY)
	{
		*ret=-1;
		return EBADF;
	}

	lock_acquire(file->file_lock);

	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = bufsize;		 // length of the memory space
	u.uio_iov = &iov;
	u.uio_iovcnt = 1;
	u.uio_resid = bufsize;          // amount to read from the file
	u.uio_offset = file->file_offset;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE;
	u.uio_space = curthread->t_addrspace;

	result = VOP_WRITE(v, &u);
	if (result) {
		lock_release(file->file_lock);
		*ret=-1;
		return result;
	}

	file->file_offset=u.uio_offset;
	*ret=bufsize-u.uio_resid;
	lock_release(file->file_lock);

	return 0;
}


/*
 * sys_close system call: close file
 */
int
sys___close(int *ret, int fd)
{
	if(fd<0 || fd>OPEN_MAX || fd>curthread->fd_count+2)
	{
		*ret=-1;
		return EBADF;
	}

	struct vnode *v=curthread->f_handles[fd]->file_ref;

	if(curthread->f_handles[fd]->file_counter==1)
	{
		//vfs_close(v);
		VOP_CLOSE(v);
		lock_destroy(curthread->f_handles[fd]->file_lock);
		kfree(curthread->f_handles[fd]);
		curthread->f_handles[fd]=NULL;
		curthread->fd_count--;
	}
	else
		curthread->f_handles[fd]->file_counter--;



		// error
		*ret=0;
		return 0;

}

int
sys___dup2(int *ret, int oldfd, int newfd)
{
	//kprintf("old fd is %d,%d,%d",oldfd,newfd,curthread->fd_count);
	if(oldfd<0 || oldfd>=OPEN_MAX || newfd<0 || newfd>=OPEN_MAX)
	{
		*ret=-1;
		return EBADF;
	}
	if(oldfd==newfd)
	{
		*ret=oldfd;
		return 0;
	}
	if(oldfd>curthread->fd_count+2)
	{
		*ret=-1;
		return EBADF;
	}

	if(newfd<=curthread->fd_count+2 && curthread->f_handles[newfd]!=NULL)
		sys___close(ret,newfd);

	lock_acquire(curthread->f_handles[oldfd]->file_lock);
	if(curthread->fd_count+2<newfd)
		curthread->fd_count=newfd-2;
	curthread->f_handles[newfd]=kmalloc(sizeof(*curthread->f_handles[newfd]));
	curthread->f_handles[newfd]->file_counter=curthread->f_handles[oldfd]->file_counter;
	curthread->f_handles[newfd]->file_name=kstrdup(curthread->f_handles[oldfd]->file_name);
	curthread->f_handles[newfd]->file_offset=curthread->f_handles[oldfd]->file_offset;
	curthread->f_handles[newfd]->file_ref=curthread->f_handles[oldfd]->file_ref;
	curthread->f_handles[newfd]->op_flags=curthread->f_handles[oldfd]->op_flags;
	curthread->f_handles[newfd]->file_lock=lock_create("dup2");
	lock_release(curthread->f_handles[oldfd]->file_lock);
	*ret=newfd;
	return 0;

}

int
sys___chdir(int *ret, char *dirname)
{
	char *dname;

	if(dirname==NULL)
	{
		*ret=-1;
		return EFAULT;
	}
	size_t len;
	int result= copyinstr((const_userptr_t) dirname, dname, PATH_MAX, &len);

	if (result)
	{
		*ret=-1;
		return result;
	}

	if(dname==NULL)
	{
		*ret=-1;
		return EFAULT;
	}
	result = vfs_chdir(dirname);
		if (result) {
			*ret=result;
			return -1;
		}
		*ret=0;
		return 0;
}
int
sys___getcwd(int *ret, char *buf, size_t buflen)
{
	struct iovec iov;
	struct uio ku;

	if(buf==NULL)
	{
			*ret=-1;
			return EFAULT;
	}
	void *kbuf=kmalloc(sizeof(*buf)*buflen);
	if(kbuf==NULL)
	{
		*ret=-1;
		return EFAULT;
	}
	size_t len;
	int result = copycheck2((const_userptr_t) buf, buflen, &len);
	if (result) {
		return result;
	}

		result= copyinstr((const_userptr_t) buf, kbuf, buflen, &len);
		if (result)
		{
			*ret=-1;
			return result;
		}


	uio_kinit(&iov, &ku, buf, buflen, 0, UIO_READ);
	result = vfs_getcwd(&ku);
	if (result) {
		*ret=-1;
		return result;
	}

	/* null terminate */
	buf[sizeof(buf)-1-ku.uio_resid] = 0;
	*ret=strlen(buf);
	return 0;
}
int
sys___lseek(int *ret,int *ret2, int fd, off_t offset, int whence)
{
	off_t new_pos,fsize;
	if(fd<0 || fd>OPEN_MAX || curthread->f_handles[fd]==NULL)
	{
		*ret=-1;
		return EBADF;
	}
	if(fd>curthread->fd_count+2)
	{
		//kprintf("fd is %d,%d",fd,curthread->fd_count);
		*ret=-1;
		return EBADF;
	}
/*	if(fd>=0 && fd<3)
	{
		*ret=-1;
		return ESPIPE;
	}
*/
	struct stat fs;

	lock_acquire(curthread->f_handles[fd]->file_lock);
	int result=VOP_STAT(curthread->f_handles[fd]->file_ref,&fs);
	if(result)
	{
		*ret=-1;
		lock_release(curthread->f_handles[fd]->file_lock);
		return result;
	}
	fsize=fs.st_size;
	switch(whence)
	{
	case SEEK_SET:
		new_pos=offset;
		break;
	case SEEK_END:
		new_pos=fsize+offset;
		break;
	case SEEK_CUR:
		new_pos=curthread->f_handles[fd]->file_offset+offset;
		break;
	default:	*ret=-1;
		lock_release(curthread->f_handles[fd]->file_lock);
		return EINVAL;
	}
	if(new_pos<0)
	{
		*ret=-1;
		lock_release(curthread->f_handles[fd]->file_lock);
		return EINVAL;
	}
	result=VOP_TRYSEEK(curthread->f_handles[fd]->file_ref,new_pos);
	if(result)
	{
		*ret=-1;
		lock_release(curthread->f_handles[fd]->file_lock);
		return result;
	}
	curthread->f_handles[fd]->file_offset=new_pos;

	lock_release(curthread->f_handles[fd]->file_lock);

	*ret=(new_pos & 0xFFFFFFFF00000000) >> 32;
	*ret2=new_pos & 0xFFFFFFFF;

	return 0;
}
