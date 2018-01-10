#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

//this is my code for project from here - isaac

struct proc *pQueue[3][NPROC];
int head[3] = {0, 0, 0};
int MLFQ_ticks = 0;
struct proc* next_MLFQ;

struct stride {
    int tickets;
    int stride;
    int pass;

    struct proc* p;
};

struct spinlock mem_lock;      //lock for management of memory  - isaac

struct stride MLFQ;
struct stride stable[NPROC];
struct stride* next_stride = &MLFQ;

struct proc* insertPqueue(int pLev, struct proc* p);
struct proc* deletePqueue(struct proc* p);
struct stride* delete_stride(struct proc* p);

int nexttid = 1;
//this is my code for project2 to here - isaac


struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

    

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
  initlock(&mem_lock,"memory lock");
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);

  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  p->pLev = 0;
  p->q_ticks = 0;
  p->stride = &MLFQ;

  p->tid = 0;
  p->joinedthread = 0;
  p->retval = 0;
  p->t_num = 0;
  p->t_head = p;

  insertPqueue(0, p);
  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc* master;

  master = proc;
  if(proc->tid != 0)
      master = proc->parent;

  acquire(&mem_lock);
  sz = master->sz;
  if(n > 0){
    if((sz = allocuvm(master->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(master->pgdir, sz, sz + n)) == 0)
      return -1;
  }

  master->sz = sz;
  release(&mem_lock);

  switchuvm(proc);
  return 0;
}


// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  if((np->pgdir = copyuvm(proc->pgdir, proc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }

  np->sz = proc->sz;
  np->parent = proc;
  *np->tf = *proc->tf;
  
  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(proc->ofile[i])
      np->ofile[i] = filedup(proc->ofile[i]);
  np->cwd = idup(proc->cwd);

  safestrcpy(np->name, proc->name, sizeof(proc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);


  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *p;
  struct proc *master;  //this is master thread - isaac
  int fd;

  master = proc;        // set master thread - isaac
  if(proc->tid != 0)
      master = proc->parent;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(master->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == master && p->tid == 0){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p == master || (p->parent == master && p->tid !=0)){
          
          release(&ptable.lock);
          for(fd = 0; fd < NOFILE; fd++){
              if(p->ofile[fd]){
                  fileclose(p->ofile[fd]);
                  p->ofile[fd] = 0;
              }
          }

          begin_op();
          iput(p->cwd);
          end_op();
          p->cwd = 0;
          acquire(&ptable.lock);
          
          p->state = ZOMBIE;
      }
  }

  // Jump into the scheduler, never to return.
  
  if(master->stride == &MLFQ) {    
        deletePqueue(master);
  } else {
      delete_stride(master);
  }

  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  struct proc *t;
  int havekids, pid, i;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != proc || p->tid != 0)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
          for(t = ptable.proc; t < &ptable.proc[NPROC]; t++){
              if(t->parent == p && t->tid != 0){
                  kfree(t->kstack);
                  t->kstack = 0;
                  t->pid = 0;
                  t->parent = 0;
                  t->name[0] = 0;
                  t->killed = 0;
                  t->state = UNUSED;
                  t->tid = 0;
                  t->retval = 0;
                  t->joinedthread = 0;
              }
          }


        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->t_num = 0;
        p->t_head = 0;
        for(i = 0; i < NPROC; i++){
            p->free_list[i] = 0;
        }
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}

//this is my code for project2 from here - isaac
struct proc* insertPqueue(int pLev, struct proc* p)
{
	
	int searchHead = head[pLev];

    int initHead = searchHead;
    do{
        if(pQueue[pLev][searchHead] == 0){
            pQueue[pLev][searchHead] = p;
            p->pLev = pLev;
            p->q_ticks = 0;
            return p;
        }
        searchHead = (searchHead + 1) % NPROC;
    }while(searchHead != initHead);

    cprintf("insertPqueue ERROR\n");
    return 0;
}

struct proc* deletePqueue(struct proc* toDelete)
{
	struct proc *p;
	int i;
    int pLev = toDelete->pLev;

	for(i=0; i<NPROC; i++) {
		p = pQueue[pLev][i];
		if(p != 0)
			if( p->pid == toDelete->pid){
				pQueue[pLev][i] = 0;
                return p;
			}
	}
    cprintf("deletePqueue ERROR\n");
	return 0;
}

struct proc* getHighestProc()
{
	struct proc *p;
	int pLev, initHead;
	for(pLev = 0; pLev < 3; pLev++){
		initHead=head[pLev];
		do{
			p=pQueue[pLev][head[pLev]];

			if(p != 0)
				if(p->state == RUNNABLE || p->t_num != 0){
					return p;
                }

			head[pLev] = (head[pLev]+1) % NPROC;

		}while(initHead != head[pLev]);
	}
	return 0;	
}

void priorityBoost()
{
	struct proc *p;
	int pLev, i;
    
    //cprintf("[do boosting!]\n");

    MLFQ_ticks = 0;
	for(pLev = 0; pLev < 3; pLev++){
		for(i = 0; i < NPROC; i++){
			pQueue[pLev][i] = 0;
		}
        head[pLev] = 0;
	}

	acquire(&ptable.lock);
    
	for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
		if(p->state != UNUSED && p->stride == &MLFQ) { //nayong
			insertPqueue(0,p);
		}
	}

    release(&ptable.lock);
}

void check(void){
    int i, j;
    acquire(&mem_lock);
    for(i = 0; i<NPROC; i++){
        cprintf("[%d's state = %d] ",i,ptable.proc[i].state);
        j++;
        if(j%10 == 0)
            cprintf("\n");
    }
    release(&mem_lock);
}

void MLFQ_handler(void)
{
    struct proc *p = proc;
    if(proc->tid != 0)
        p = proc->parent;

    if(p->stride != &MLFQ) { 
        return;
    }


	p->q_ticks++;
	MLFQ_ticks++;
    

	if(MLFQ_ticks >= 100) {
        priorityBoost();
		return;
	}

	switch(p->pLev){
	case 0:
		if(p->q_ticks >= 5){
			head[0] = (head[0] + 1) % NPROC;
		    deletePqueue(p);
            p->pLev++;
            insertPqueue(p->pLev,p);
		}
		break;

	case 1:
		if(p->q_ticks >= 10){	
			head[1] = (head[1] + 1) % NPROC;
		    deletePqueue(p);
            p->pLev++;
            insertPqueue(p->pLev,p);
        }
		break;

	case 2:
		if(p->q_ticks >= 20){
			head[2] = (head[2] + 1) % NPROC;
			p->q_ticks = 0;
		}
		break;
	}
}

struct stride* delete_stride(struct proc* toDelete)
{
    struct stride* s;
    for (s = stable; s < &stable[NPROC]; s++) {
        if(s->p != 0)
            if(s->p->pid == toDelete->pid) {
                s->p = 0;
                toDelete -> stride = 0;
          
                return s;
            }
    }
    cprintf("ERROR: there is no %d process in stride table\n",toDelete->pid);
    return 0;
}


int set_cpu_share(int n) {
    int total = n;
    struct stride *s;
    struct proc *p = proc;
    if(proc->tid != 0)
        p = proc->parent;
    
    if(n<=0) {
        cprintf("ERROR: cpu time argu <= 0\n");
        return -1;
    }

    for (s = stable; s < &stable[NPROC]; s++) {
        if(s->p != 0)
            total += s->tickets;
    }
    if(total > 80) {
        cprintf("ERROR: stride exeed 80%% of CPU time\n");
        return -1;
    }


    MLFQ.tickets = 100;
    MLFQ.tickets -= total;
    MLFQ.stride = LARGE_NUM / MLFQ.tickets;
    MLFQ.pass = 0;

    MLFQ.p = 0;

    for (s = stable; s < &stable[NPROC]; s++) {
        if(s->p != 0)
            s->pass = 0;
    }

    for (s = stable; s < &stable[NPROC]; s++) {
        if(s->p == 0)
            break;
    }
    
    s->tickets = n;
    s->stride = LARGE_NUM / n;
    s->pass = 0;
    s->p = p;
    
    p->stride = s;
    deletePqueue(p);

    return n;
}

struct stride* getLowestStride()
{
    struct stride* choice = &MLFQ;
    struct stride* s;

    if( getHighestProc() == 0 )
        choice = stable;

    for(s = stable; s < &stable[NPROC]; s++) {
        if(s->p != 0) {
            if(s->pass < choice->pass){
                if(s->p->state == RUNNABLE || s->p->t_num != 0)
                    choice = s;
            }
        }
    }
    return choice;
}

void stride_reset(void)
{
    struct stride* s;
    MLFQ.pass = 0;
    for (s = stable; s < &stable[NPROC]; s++) {
        if(s->p != 0)
            s->pass = 0;
    }
}

void stride_handler(void)
{
    struct stride* s = proc->stride;
    if(proc->tid != 0)
        s = proc->parent->stride;

    s->pass += s->stride;
    
    if(s->pass > 100000000)
      stride_reset();

    return;
}

int thread_create(thread_t * thread, void * (*start_routine)(void *), void *arg)
{
    int i;
    struct proc *nt;
    int *free;
    struct proc *master;

    master = proc;
    if(proc->tid != 0)
        master = proc->parent;

    // allocate new thread
    if((nt = allocproc()) == 0){
        cprintf("쓰레드 할당실패\n");
        return -1;
    }
    
    deletePqueue(nt);
    nt->stride = 0;

    nt->parent = master;
    nt->pgdir = master->pgdir;
    *nt->tf = *master->tf;

    nt->tf->eax = 0;

    for(i = 0; i < NOFILE; i++)
        if(master->ofile[i])
            nt->ofile[i] = filedup(master->ofile[i]);
    nt->cwd = idup(master->cwd);
    
    safestrcpy(nt->name, master->name, sizeof(master->name));
    
    acquire(&mem_lock);
    for(free = master->free_list; free < &master->free_list[NPROC]; free++){
        if(*free != 0)
        {
            if(allocuvm(master->pgdir, *free-PGSIZE, *free) == 0){
                cprintf("thread_create : Memory allocate error\n");
                return -1;
            }
            nt->sz = *free;
            *free = 0;
            break;
        }
    }
    
    if(free >= &master->free_list[NPROC]){
        if(allocuvm(master->pgdir, master->sz, master->sz + PGSIZE) == 0){
            cprintf("thread_create : Memory allocate error\n");
            return -1;
        }
        master->sz = master->sz + PGSIZE;

        nt->sz = master->sz;
    }
    release(&mem_lock);
    
    nt->tf->eip = (uint)start_routine;
    nt->tf->esp = nt->sz;
    nt->tf->esp -= 12;
    *(uint*)(nt->tf->esp + 8) = 0;
    *(uint*)(nt->tf->esp + 4) = (uint)arg;
    *(uint*)(nt->tf->esp) = 0xffffffff;

    nt->tid = nexttid++;
    *thread = nt->tid;
    master->t_num++;

    acquire(&ptable.lock);
    nt->state = RUNNABLE;
    release(&ptable.lock);
    //cprintf("%d is pid\n\n",nt->tid);
    return 0;
}

void thread_exit(void *retval){

  struct proc *p;
  int fd;

  proc->retval = retval;

  if(proc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(proc->ofile[fd]){
      fileclose(proc->ofile[fd]);
      proc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(proc->cwd);
  end_op();
  proc->cwd = 0;

  acquire(&ptable.lock);

  // joined thread might be sleeping in wait().
  wakeup1(proc->joinedthread);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == proc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  proc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

int thread_join(thread_t thread, void **retval)
{
    struct proc *p;
    int *free;

    acquire(&ptable.lock);
    for(p = ptable.proc; p <= &ptable.proc[NPROC]; p++){
        if(p==&ptable.proc[NPROC])
            return -1;
        if(p->tid == thread)
            break;
    }

    p->joinedthread = proc;
    
    while(p->state != ZOMBIE){
        sleep(proc,&ptable.lock);
    }
    if(retval != 0) *retval = p->retval;
    
    acquire(&mem_lock);
    for(free = p->parent->free_list; free < &p->parent->free_list[NPROC]; free++)
    {
        if(*free == 0){
            *free = p->sz;
            break;
        }
    }
    //cprintf("%d joind\n",p->tid);
    
    if(deallocuvm(p->pgdir, p->sz, p->sz - PGSIZE) == 0){
        cprintf("thread_join: deallocuvm error\n");
    }
    release(&mem_lock);

    kfree(p->kstack);
    p->kstack = 0;
    p->pid = 0;
    p->parent = 0;
    p->name[0] = 0;
    p->killed = 0;
    p->state = UNUSED;
    p->tid = 0;
    p->joinedthread = 0;
    p->retval = 0;
    
    p->parent->t_num--;
    release(&ptable.lock);
    return 0;
}
/*{
  struct proc *p;
  int thread_exist;

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    thread_exist = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->tid != thread)
        continue;
      thread_exist = 1;
      p->joinedthread = proc;
      if(p->state == ZOMBIE){
        // clean thread.
        t_growproc(p,-PGSIZE);
        kfree(p->kstack);
        p->kstack = 0;
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->tid = 0;
        p->joinedthread = 0;
        p->retval = 0;
        release(&ptable.lock);
        return 0;
      }
    }

    // No point waiting if we don't have that thread.
    if(!thread_exist || proc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for thread to exit.  (See wakeup1 call in proc_exit.)
    sleep(proc, &ptable.lock);  //DOC: wait-sleep
  }
}
*/

void
thread_zombie(pde_t *pgdir){
    struct proc *t;
    for(t = ptable.proc; t < &ptable.proc[NPROC]; t++){
        if(t->pgdir == pgdir && t->tid != 0){
            t->state=ZOMBIE;
        }
    }
}


//this is my code for project2 to here - isaac


//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct proc *my_pick;
  struct proc *t;
  struct proc *temp;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      
      if((next_stride = getLowestStride())->p != 0)
          my_pick = next_stride->p;
      else if((next_MLFQ = getHighestProc()) != 0)
          my_pick = next_MLFQ;
      
      if(my_pick->t_num != 0){
        temp = my_pick;
        for(t = my_pick->t_head; t < &ptable.proc[NPROC]; t++){
            if(t->state == RUNNABLE && t->tid != 0 && t->parent == my_pick){
                my_pick->t_head = t+1;
                my_pick = t;
                break;
            }
        }
        if(my_pick==temp)
            my_pick->t_head = ptable.proc;
      }

      if(my_pick->state == RUNNABLE){
          p=my_pick;
      }

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      proc = p;
      switchuvm(p);
      p->state = RUNNING;
      swtch(&cpu->scheduler, p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(cpu->ncli != 1)
    panic("sched locks");
  if(proc->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = cpu->intena;
  swtch(&proc->context, cpu->scheduler);
  cpu->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  proc->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  if(proc == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }

  // Go to sleep.
  proc->chan = chan;
  proc->state = SLEEPING;
  sched();

  // Tidy up.
  proc->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}
