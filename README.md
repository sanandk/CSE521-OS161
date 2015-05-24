
#CSE 521/421 - Operating Systems
###Programming Assignments

####Introduction to OS/161
As an instructional operating system OS/161 strikes a balance between simplicity and realism. Operating system enviroments that are too simple or do not expose you to the difficulties of kernel programming fail to provide a realistic introduction to operating systems development. Implementing kernel-like functionality in userspace is uninteresting and not something that we will do in this class. All of the OS/161 code you will write for ops-class.org will be part of the operating system itself, not userspace applications.

On the other hand, environments that are too realistic are frequently so complex that it is difficult to learn anything quickly. Just understanding the structure of Linux virtual memory system would take most of a semester, much less getting to the point where you could make meaningful modifications. OS/161 allows you to implement a basic virtual memory subsystem as a month-long assignment. You will be surprised how difficult it is to correctly provide even a small subset of the functionality that would be implemented by a mature, POSIX-compliant operating system.

Your OS/161 distribution contains a full operating system source tree, including some utility programs and libraries. It is important to realize that every piece of code used to build your kernel and userspace tools is located in your OS/161 source tree. There are no external libraries, binary blobs, or other external dependencies. Everything is in there, so if you want to see how something works you have the code required to do so.

####ASST0: Introduction to OS/161
Introduces you to the programming environment you will be working in this semester, including the OS/161 operating system, the sys161 simulator, the GNU debugger (GDB), and the Git revision control system. Consists of code reading questions, a few simple scripting tasks, and a very simple implementation task.

####ASST1: Synchronization
Your first real taste of kernel programming. After completing a set of code reading questions, you implement locks, condition variables and reader-writer locks. Next, you use them to solve a few simple toy synchronization problems.

####ASST2: System Calls and Process Support
The first big and complex assignment. Start by submitting a design that indicates you understand all of the moving pieces and what to do. Next, implement the system call interface. When you are finished, your kernel should be able to run user programs.

####ASST3: Virtual Memory
The summit of the CSE 421/521 mountain. A large amount of code to implement and many internal interfaces to design. As always, start with a careful design. Then implement virtual memory, including address translation, TLB management, page replacement and swapping. When you are finished, your kernel should be able to run forever without running out of memory, and you will have completed the course.
