/*
 * Copyright (c) 2001, 2002, 2009
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

/*
 * Driver code for whale mating problem
 */
#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>
#include <current.h>

/*
 * 08 Feb 2012 : GWA : Driver code is in kern/synchprobs/driver.c. We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.
struct semaphore *malesem;
struct semaphore *femalesem;
struct semaphore *matchmakersem,*testsem, *testsem2, *testsem3;
struct lock *jlock;
struct semaphore *mutex;

int j[4], head[4];


int male_cnt,female_cnt,match_cnt;

void whalemating_init() {
	malesem = sem_create("Whalemating Male Semaphore",0);
	femalesem = sem_create("Whalemating Female Semaphore",0);
	matchmakersem = sem_create("Whalemating MatchMaker Semaphore",0);

	if (testsem==NULL) {
			testsem = sem_create("testsem", 0);
			if (testsem == NULL) {
				panic("synchtest: sem_create failed\n");
			}
		}
	if (testsem2==NULL) {
			testsem2= sem_create("testsem2", 0);
				if (testsem2== NULL) {
					panic("synchtest: sem_create failed\n");
				}
			}
	if (testsem3==NULL) {
			testsem3= sem_create("testsem3", 0);
				if (testsem3== NULL) {
					panic("synchtest: sem_create failed\n");
				}
			}
	male_cnt=0;
	female_cnt=0;
	match_cnt=0;

  return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void whalemating_cleanup() {
  return;
}

void
male(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;
	male_start();
	V(malesem);
	P(testsem);

	//P(matchmakersem);

	/*V(testsem);
	V(testsem);
	V(testsem);
	P(testsem);
	*/
	//male_cnt++;
	male_end();
	//V(testsem2);


	//V(matchmakersem);
	//V(femalesem);




  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

void
female(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;
  female_start();

    V(femalesem);
	//P(malesem);
		P(testsem2);

/*		V(testsem);
		V(testsem);
		V(testsem);
		P(testsem);

	V(testsem);
		V(testsem2);
		V(testsem2);
		V(testsem2);
		P(testsem2);
*/
	female_end();
	/*V(testsem2);
*/
	//V(matchmakersem);
	//V(malesem);


  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

void
matchmaker(void *p, unsigned long which)
{
	struct semaphore * whalematingMenuSemaphore = (struct semaphore *)p;
  (void)which;
    matchmaker_start();

//	V(matchmakersem);
	P(malesem);
	P(femalesem);


	V(testsem);
	V(testsem2);
	/*P(testsem);


	V(testsem);
	V(testsem2);
	V(testsem2);
	V(testsem2);
	P(testsem2);
*/
	matchmaker_end();
	/*V(testsem2);

	V(femalesem);
	V(malesem);
*/

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // whalemating driver can return to the menu cleanly.
  V(whalematingMenuSemaphore);
  return;
}

/*
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is,
 * of course, stable under rotation)
 *
 *   | 0 |
 * --     --
 *    0 1
 * 3       1
 *    3 2
 * --     --
 *   | 2 | 
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X
 * first.
 *
 * You will probably want to write some helper functions to assist
 * with the mappings. Modular arithmetic can help, e.g. a car passing
 * straight through the intersection entering from direction X will leave to
 * direction (X + 2) % 4 and pass through quadrants X and (X + 3) % 4.
 * Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in drivers.c.
 */

// 13 Feb 2012 : GWA : Adding at the suggestion of Isaac Elbaz. These
// functions will allow you to do local initialization. They are called at
// the top of the corresponding driver code.
#define DIRECTIONS 4
struct semaphore *quadSem[DIRECTIONS];  //Array Size indicates the number of directions
/*
struct semaphore *quadSem1;
struct semaphore *quadSem2;
struct semaphore *quadSem3;
*/

void stoplight_init() {

  for(int i=0; i<DIRECTIONS;i++)
  {
    if(quadSem[i]==NULL){
      quadSem[i] = sem_create("Quadrant Semaphore",1);
      if (quadSem[i] == NULL) {
        panic("synchtest: sem_create failed at quadSem\n");
      }
    }
  }

  if(mutex==NULL){
	  mutex = sem_create("mutex Semaphore",1);
        if (mutex == NULL) {
          panic("synchtest: sem_create failed at quadSem\n");
        }
      }
  j[0]=0;
  j[1]=0;
  j[2]=0;
  j[3]=0;
  head[0]=1;
  head[1]=1;
  head[2]=1;
  head[3]=1;

  jlock=lock_create("j1lock");
  /*jl[1]=lock_create("j2lock");
  jl[2]=lock_create("j3lock");
  jl[3]=lock_create("j4lock");
*/
/*  if(quadSem1==NULL){
    quadSem1 = sem_create("Quadrant 1 Semaphore",0);
    if (quadSem1 == NULL) {
      panic("synchtest: sem_create failed at quadSem1\n");
    }
  }
  if(quadSem2==NULL){
    quadSem2 = sem_create("Quadrant 2 Semaphore",0);
    if (quadSem2 == NULL) {
      panic("synchtest: sem_create failed at quadSem2\n");
    }
  }
  if(quadSem3==NULL){
    quadSem3 = sem_create("Quadrant 3 Semaphore",0);
    if (quadSem3 == NULL) {
      panic("synchtest: sem_create failed at quadSem3\n");
    }
  }
*/
  return;
}

// 20 Feb 2012 : GWA : Adding at the suggestion of Nikhil Londhe. We don't
// care if your problems leak memory, but if you do, use this to clean up.

void stoplight_cleanup() {
  for(int i=0; i<DIRECTIONS;i++)
  {
    if(quadSem[i]!=NULL){
      sem_destroy(quadSem[i]);
    }
  }
/*
  if(quadSem1!=NULL){
    sem_destroy(*quadSem1);
  }
  if(quadSem2!=NULL){
    sem_destroy(*quadSem2);
  }
  if(quadSem3!=NULL){
    sem_destroy(*quadSem3);
  }
*/
  return;
}

void
gostraight(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
  (void)direction;
//kprintf("\nGo Straight: %s, %ld\n",curthread->t_name,direction);
/*
while(1)
{
lock_acquire(jlock);
if(quadSem[direction]->sem_count==1 && quadSem[(direction+3)%4]->sem_count==1)
{
	P(quadSem[direction]);
	P(quadSem[(direction+3)%4]);
	break;
}
lock_release(jlock);
}*/
//int j_id= ++j[direction];
//while(j_id!=head[direction]);

while(1)
{
	P(mutex);
  P(quadSem[direction]);
  //ex_flag++;
  //if(ex_flag==2)
	  //break;
if(quadSem[(direction+3)%4]->sem_count!=0)
{
	P(quadSem[(direction+3)%4]);

	V(mutex);
	break;
}
else
{
	V(quadSem[direction]);
	V(mutex);
}

}

  inQuadrant(direction);
  

  //Entering the second quadrant

  inQuadrant((direction+3)%4);


  //exiting
  leaveIntersection();
  //head[direction]++;
//  removeHead(j[direction],jl[direction]);
  V(quadSem[direction]);
  V(quadSem[(direction+3)%4]);

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}

void
turnleft(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;
//	int j_id= ++j[direction];
//	while(j_id!=head[direction]);
//kprintf("\nTurning left: %s, %ld\n",curthread->t_name,direction);
/*while(1)
{
lock_acquire(jlock);
if(quadSem[direction]->sem_count==1 && quadSem[(direction+3)%4]->sem_count==1 && quadSem[(direction+2)%4]->sem_count==1)
{
	P(quadSem[direction]);
	P(quadSem[(direction+3)%4]);
	P(quadSem[(direction+2)%4]);
	break;
}
lock_release(jlock);
}*/
//insertEnd(j[direction],jl[direction],curthread->t_name);
//int ex_flag=0;
//while(!am_i_first(j[direction],curthread->t_name));

while(1)
{
	P(mutex);
	//Entering the first quadrant
  P(quadSem[direction]);

//kprintf("\nTurning left: %s acquired %ld\n",curthread->t_name,direction);

  //Entering the second quadrant;
if(quadSem[(direction+3)%4]->sem_count>0)
{
  P(quadSem[(direction+3)%4]);
if(quadSem[(direction+2)%4]->sem_count>0)
{
  P(quadSem[(direction+2)%4]);
  	  V(mutex);
	break;
}
else
{
	V(quadSem[(direction+3)%4]);
	V(quadSem[direction]);
}
}
else
	V(quadSem[direction]);
V(mutex);
//kprintf("\nTurning left: %s acquired %ld\n",curthread->t_name,(direction+3)%4);

}
//kprintf("\nTurning left: %s acquired %ld\n",curthread->t_name,(direction+2)%4);

  inQuadrant(direction);
  
  inQuadrant((direction+3)%4);

  inQuadrant((direction+2)%4);

  //exiting
  leaveIntersection();
//  head[direction]++;
//  removeHead(j[direction],jl[direction]);
  V(quadSem[direction]);
  V(quadSem[(direction+3)%4]);
  V(quadSem[(direction+2)%4]);


  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}

void
turnright(void *p, unsigned long direction)
{
	struct semaphore * stoplightMenuSemaphore = (struct semaphore *)p;

	//int j_id= ++j[direction];
	//while(j_id!=head[direction]);
//  while(!am_i_first(j[direction],curthread->t_name));
//kprintf("\nTurning right: %s, %ld\n",curthread->t_name,direction);
//insertEnd(j[direction],jl[direction],curthread->t_name);
  //Entering the first and final quadrant

	P(mutex);
	P(quadSem[direction]);
	V(mutex);

//kprintf("\nTurning right: %s acquired %ld\n",curthread->t_name,direction);
  inQuadrant(direction);

  //exiting
  leaveIntersection();
  //head[direction]++;
//  removeHead(j[direction],jl[direction]);
  V(quadSem[direction]);

  // 08 Feb 2012 : GWA : Please do not change this code. This is so that your
  // stoplight driver can return to the menu cleanly.
  V(stoplightMenuSemaphore);
  return;
}
