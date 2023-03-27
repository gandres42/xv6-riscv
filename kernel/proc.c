#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// system parameters for fair scheduler
int cfs_sched_latency = 100;
int cfs_max_timeslice = 10;
int cfs_min_timeslice = 1;

// nice to weight conversion table
// convert nice val x to a weight using nice_to_weight[x + 20]
int nice_to_weight[40] = { 
  88761, 71755, 56483, 46273, 36291,  /*for nice = -20, ..., -16*/ 
  29154, 23254, 18705, 14949, 11916, /*for nice = -15, ..., -11*/ 
  9548, 7620, 6100, 4904, 3906, /*for nice = -10, ..., -6*/ 
  3121, 2501, 1991, 1586, 1277, /*for nice = -5, ..., -1*/ 
  1024, 820, 655, 526, 423, /*for nice = 0, ..., 4*/ 
  335, 272, 215, 172, 137, /*for nice = 5, ..., 9*/ 
  110, 87, 70, 56, 45, /*for nice = 10, ..., 14*/ 
  36, 29, 23, 18, 15, /*for nice = 15, ..., 19*/ 
};

// variables for fair scheduler use
int cfs = 0; //indicate if the fair scheduler is the current scheduler, 0 by default 
struct proc * cfs_current_proc = 0; //the process currently scheduled to run by the fair scheduler and is initialized to 0 
int cfs_proc_timeslice_len = 0; //number of timeslices assigned to the above process
int cfs_proc_timeslice_left = 0; //number of timeslices that the above process can still run

// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table.
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;
  p->swapcount = 0;
  p->nice = 0;
  p->vruntime = 0;
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// TODO
int weight_sum()
{
  struct proc *p;
  int total_weight = 0;

  for(p = proc; p < &proc[NPROC]; p++) {
    // acquire(&p->lock);
    if(p->state == RUNNABLE) {
      total_weight += nice_to_weight[p->nice + 20];
    }
    // release(&p->lock);
  }

  return total_weight;
}

// TODO
struct proc * shortest_runtime_proc()
{
  struct proc *p;
  struct proc *sp = 0;

  for(p = proc; p < &proc[NPROC]; p++) {
    // acquire(&p->lock);
    if(p->state == RUNNABLE) {
      if (sp == 0 || p->vruntime < sp->vruntime)
      {
        sp = p; 
      }
    }
    // release(&p->lock);
  }

  return sp;
}

void cfs_scheduler(struct cpu *c) 
{ 
  //initialize c->proc, which is the process to be run in the next timeslice 
  c->proc = 0; 

  //decrement the current process’ left timeslice 
  cfs_proc_timeslice_left -=1;  

  if(cfs_proc_timeslice_left > 0 && cfs_current_proc->state == RUNNABLE)
  {
    //when the current process hasn’t used up its assigned timeslices and is runnable 
    //it should continue to run the next timeslce 
    c->proc = cfs_current_proc; 
  }
  else if(cfs_proc_timeslice_left == 0 || (cfs_current_proc != 0 && cfs_current_proc->state != RUNNABLE))
  {  
    //when the current process uses up its timeslices or becomes not runnable 
    //it should not be picked to run next and its vruntime should be updated 
    int weight = nice_to_weight[cfs_current_proc->nice+20]; //convert nice to weight 
    int inc = (cfs_proc_timeslice_len - cfs_proc_timeslice_left) * 1024 / weight;   
    
    //compute the increment of its vruntime according to CFS design  
    if(inc<1) inc=1; //increment should be at least 1 
    cfs_current_proc->vruntime += inc; //add the increment to vruntime 
    
    //prints for testing and debugging purposes 
    printf("[DEBUG CFS] Process %d used up %d of its assigned %d timeslices and is swapped out!\n", cfs_current_proc->pid, cfs_proc_timeslice_len - cfs_proc_timeslice_left, cfs_proc_timeslice_len); 
  } 
    
  if(c->proc==0)
  {  
    // Call shortest_runtime_proc() to get the proc with the shorestest vruntime 
    struct proc * sp = shortest_runtime_proc();

    // If (1) returns a valid process, set up cfs_current_proc, cfs_proc_timeslice_len, 
    // cfs_proc_timeslice_left and c->proc accordingly.  
    // Notes: according to CFS, a process is assigned with time slice of  
    // ceil(cfs_sched_latency * weight_of_this_process / weights_of_all_runnable_process) 
    // and the timeslice length should be in [cfs_min_timeslice, cfs_max_timeslice] 
    if (sp != 0)
    { 
      cfs_current_proc = sp;
      int sum = weight_sum();
      cfs_proc_timeslice_len = cfs_sched_latency * nice_to_weight[sp->nice + 20] / sum;
      if (cfs_sched_latency * nice_to_weight[sp->nice + 20] % sum != 0)
      {
        cfs_proc_timeslice_len += 1;
      }
      if (cfs_proc_timeslice_len > cfs_max_timeslice)
      {
        cfs_proc_timeslice_len = cfs_max_timeslice;
      }
      else if (cfs_proc_timeslice_len < cfs_min_timeslice)
      {
        cfs_proc_timeslice_len = cfs_min_timeslice;
      }     
      cfs_proc_timeslice_left = cfs_proc_timeslice_len;
      
      c->proc = sp;
    }

    if(c->proc > 0)
    { 
        //prints for testing and debugging purposes 
        printf("[DEBUG CFS] Process %d will run for %d timeslices next!\n", c->proc->pid, cfs_proc_timeslice_len); 
    
        //schedule c->process to run 
        acquire(&c->proc->lock); 
        c->proc->state=RUNNING; 
        swtch(&c->context, &c->proc->context); 
        release(&c->proc->lock); 
    } 
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
old_scheduler(struct cpu *c) 
{ 
  struct proc *p; 
  for(p = proc; p < &proc[NPROC]; p++) { 
    acquire(&p->lock); 
    if(p->state == RUNNABLE) { 
        // Switch to chosen process.  It is the process's job 
        // to release its lock and then reacquire it 
        // before jumping back to us. 
        p->state = RUNNING; 
        c->proc = p; 
        swtch(&c->context, &p->context); 
  
        // Process is done running for now. 
        // It should have changed its p->state before coming back. 
        c->proc = 0; 
    } 
    release(&p->lock); 
  } 
}

void 
scheduler(void) 
{
  struct cpu *c = mycpu(); 
  c->proc=0; 
  for(;;){ 
    // Avoid deadlock by ensuring that devices can interrupt. 
    intr_on(); 
    if(cfs){ 
      cfs_scheduler(c); 
    }else{ 
      old_scheduler(c); 
    }
  } 
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();
  p->swapcount++;

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

// gets the current running process and if valid returns the PID, else returns -1
uint64 
sys_getppid(void) 
{  
  struct proc * current_proc = myproc();
  if (current_proc == 0)
  {
    return -1;
  }
  struct proc * parent_proc = current_proc->parent;
  return parent_proc->pid;
}

// iterate over all processes in kernel stack and return child count of current process
// reads found children into buffer passed to function
// if the current process is invalid, returns -1
uint64 
sys_getcpids(void) 
{
  int child_pids[64];
  int number=0; 
  
  //get the caller’s struct proc 
  struct proc *p = myproc(); 
  if (p == 0)
  {
    return -1;
  }

  int caller_pid = p->pid;
  
  for (int i = 0; i < NPROC; i++)
  {
    if (proc[i].parent != 0 && proc[i].parent->pid == caller_pid)
    {
      child_pids[number] = proc[i].pid;
      number++;
    }
  }

  uint64 user_array; 
  argaddr(0, &user_array); 

  //copy array child_pids from kernel memory  
  //to user memory with address user_array 
  if (copyout(p->pagetable, user_array, (char *)child_pids, number*sizeof(int)) < 0) 
    return -1; 

  return number;
}

// return number of times the current process has been swapped off the CPU
// swapcount is iterated once every time sched() is run, taking the process off the CPU
int 
sys_getswapcount(void) { 
  struct proc * p = myproc();
  if (p == 0)
  {
    return 0;
  }
  else
  {
    return p->swapcount;
  }
} 

int
sys_nice(void)
{
  int new_nice; 
  argint(0, &new_nice);

  struct proc * p = myproc();

  if (new_nice <= 19 && new_nice >= -20)
  {
    acquire(&p->lock);
    p->nice = new_nice;
    release(&p->lock);
  }
  
  return p->nice;
}

int
sys_startcfs(void)
{
  cfs = 1;

  return 1;
}

int
sys_stopcfs(void)
{
  cfs = 0;
  return 1;
}