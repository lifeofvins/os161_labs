/*
 * AUthor: G.Cabodi
 * Very simple implementation of sys__exit.
 * It just avoids crash/panic. Full process exit still TODO
 * Address space is released
 */

#include <types.h>
#include <kern/unistd.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <clock.h>
#include <copyinout.h>
#include <syscall.h>
#include <lib.h>
#include <proc.h>
#include <thread.h>
#include <addrspace.h>
#include <mips/trapframe.h>
#include <current.h>
#include <synch.h>
#include <spl.h>
#include <vnode.h>
#include <vfs.h>
#include <test.h>



/*
 * system calls for process management
 */
void
sys__exit(int status)
{
#if OPT_WAITPID
  struct proc *p = curproc;
  p->p_status = status & 0xff; /* just lower 8 bits returned */
  proc_remthread(curthread);
#if USE_SEMAPHORE_FOR_WAITPID
  V(p->p_sem);
#else
  lock_acquire(p->p_lock);
  cv_signal(p->p_cv);
  lock_release(p->p_lock);
#endif
#else
  /* get address space of current process and destroy */
  struct addrspace *as = proc_getas();
  as_destroy(as);
#endif
  thread_exit();

  panic("thread_exit returned (should not happen)\n");
  (void) status; // TODO: status handling
}

int
sys_waitpid(pid_t pid, userptr_t statusp, int options)
{
#if OPT_WAITPID
  struct proc *p = proc_search_pid(pid);
  int s;
  (void)options; /* not handled */
  if (p==NULL) return -1;
  s = proc_wait(p);
  if (statusp!=NULL) 
    *(int*)statusp = s;
  return pid;
#else
  (void)options; /* not handled */
  (void)pid;
  (void)statusp;
  return -1;
#endif
}

pid_t
sys_getpid(void)
{
#if OPT_WAITPID
  KASSERT(curproc != NULL);
  return curproc->p_pid;
#else
  return -1;
#endif
}

#if OPT_FORK
/*this is the child's fork entry function*/
static void
call_enter_forked_process(void *tfv, unsigned long dummy) {
  struct trapframe *tf = (struct trapframe *)tfv;
  (void)dummy;
  enter_forked_process(tf); 
 
  panic("enter_forked_process returned (should not happen)\n");
}


/*nella sys_fork entra solo il padre*/
int sys_fork(struct trapframe *ctf, pid_t *retval) {
  struct trapframe *tf_child;
  struct proc *newp;
  int result;

  KASSERT(curproc != NULL); /*curproc sarebbe il padre*/
  
  struct proc *parent = curproc;
  
  /*debug per execv*/
#if OPT_EXECV
  kprintf("I'm the father pid = %d address = %p\n", parent->p_pid, parent);
#endif
  struct thread *thread = curthread;
  KASSERT(thread != NULL);
  newp = proc_create_runprogram(parent->p_name);
  if (newp == NULL) {
    return ENOMEM;
  }

  tf_child = kmalloc(sizeof(struct trapframe)); /*allocation*/
  if(tf_child == NULL){
    proc_destroy(newp);
    return ENOMEM; 
  }
  
  /*first of all we have to copy parent's trap frame and pass it to child thread*/
  memcpy(tf_child, ctf, sizeof(struct trapframe)); 
  
  /*then we have to copy parent's address space*/
    as_copy(curproc->p_addrspace, &(newp->p_addrspace));
  if(newp->p_addrspace == NULL){
    proc_destroy(newp); 
    return ENOMEM; /*out of memory: a memory allocation failed. This normally means that
    		    *a process has used up all the memory available to it. 
    		    *it may also mean that memory allocation within the kernel has failed.
    		    */
  }	
  

  /*linking parent/child, so that child terminated 
     on parent exit */
  newp->p_parent = parent; /*il figlio punta al padre*/
  proc_add_child(parent, newp);
  /*aggiungo il figlio alla lista dei processi figli del padre*/
  /*qui fa partire il figlio*/
  result = thread_fork(
		 curthread->t_name, newp,
		 call_enter_forked_process, 
		 (void *)tf_child, (unsigned long)0/*unused*/);

  if (result){
    proc_destroy(newp);
    kfree(tf_child);
    return ENOMEM;
  }
  

 
  *retval = newp->p_pid; /*parent returns with child's pid immediately*/
  //newp->p_pid = 0; /*child has pid = 0*/
 
  /*la fork ritorna il pid del figlio se sono nel padre, zero se sono nel figlio*/
  
  return 0;
}
#endif /*OPT_FORK*/

#if OPT_EXECV

/*PROGETTO OS161*/
void 
kfree_all(char *argv[]) {
	int i;

	for (i=0; argv[i]; i++)
		kfree(argv[i]);

}

/*PROGETTO OS161*/
size_t padding_multiple_four(char *arg) {
/*padding prima di copiare nel kernel*/
/*in MIPS i puntatori devono essere allineati a 4*/
	size_t padding = (size_t)strlen(arg) + 1;
	kprintf("Stringa: %s\n", arg);
	while(padding % 4 != 0)
		padding++;
		
	padding -= strlen(arg);
	kprintf("Padding necessario: %d caratteri\n", padding);
	/*adesso so quanto padding mettere*/
	return padding;
}
	
	

int sys_execv(char *program, char **args) {

	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	size_t dim_args, len;
	int i;
	
	/*strutture kernel*/
	char *kprogram;
	char **kargs;
	
	
	/*verifico che entrambi gli argomenti passati ad execv siano puntatori validi*/
	if (program == NULL || args == NULL) {
		return EFAULT;
	}
	
	/*trovo la dimensione del vettore: ciclo finchè non trovo un puntatore a NULL*/

	for (dim_args = 0; args[dim_args] != NULL; dim_args++);
	
	/*alloco il vettore*/
	kargs = (char **)kmalloc(dim_args * sizeof(char**));
	if (kargs == NULL) {
		return ENOMEM;
	}
	
	/*alloco ogni riga del vettore*/
	for (i = 0; i < (int)dim_args; i++) {
		len = strlen(args[i]) + 1;
		kargs[i] = kmalloc(len);
		kargs[i+1] = NULL;
		if (kargs[i] == NULL) {
			kfree_all(kargs);
			kfree(kargs);
			return ENOMEM;
		}
		/*una volta allocato l'elemento i-esimo del vettore facico copyin da user a kernel*/
		result = copyin((const_userptr_t)args[i], (void *)kargs[i], len);
		if (result) {
			kfree_all(kargs);
			kfree(kargs);
			return -result;
		}
		/*debug*/
		
		//kprintf("kargs[%d] = %s\n", i, kargs[i]);
	}
	
	
	/*faccio la stessa cosa col nome del programma*/
	len = strlen(program) + 1;
	kprogram = kmalloc(len);
	if (kprogram == NULL) {
		kfree(kprogram);
		return ENOMEM;
	}
	result = copyin((const_userptr_t)program, (void *)kprogram, len);
	if (result) {		
		kfree_all(kargs);
		kfree(kargs);
		kfree(kprogram);
		return -result;
	}
	/*debug*/
	//kprintf("kprogram = %s\n", kprogram);
	
	/*address space handling*/
	//struct addrspace *oldas = proc_getas();
	//curproc_setas();
	//as_activate();
	/*t_as lo devo aggiungere io(?)*/
	as_destroy(curproc->p_addrspace); /* old addrspace is no longer needed */
	//as_prova = curproc->p_addrspace;
	curproc->p_addrspace = as_create(); //ricrea l'as
	if (curproc->p_addrspace==NULL) {
		return ENOMEM;
	}
	/* Open the file. */
	result = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (result) {
		as_destroy(curproc->p_addrspace);
		kfree_all(kargs);
		kfree(kargs);
		kfree(kprogram);
		return -result;
	}
	/* Switch to it and activate it. */
	proc_setas(curproc->p_addrspace);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	
	if (result) {
		as_destroy(curproc->p_addrspace);
		kfree_all(kargs);
		kfree(kargs);
		kfree(kprogram);
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);
	/* Define the user stack in the address space */
	result = as_define_stack(curproc->p_addrspace, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return -result;
	}
	
	/*devo tornare da kernel a user: qui devo usare il padding*/
	/*strutture user dopo kernel*/
	char **uargs;
	unsigned int cpaddr;
	int length, tail;
	
	//userptr_t args_out_addr;
	uargs = (char **)kmalloc(sizeof(char *) * (dim_args + 1));
	if (uargs == NULL) {
		as_destroy(curproc->p_addrspace);
		kfree_all(kargs);
		kfree(kargs);
		kfree(kprogram);
		return ENOMEM;
	}
	cpaddr = stackptr;
	for (i = 0; i < (int)dim_args; i++) {
		length = strlen(kargs[i])+1;
		cpaddr -= length;
		tail = 0;
		/*padding*/
		if (cpaddr & 0x3) {
			tail = cpaddr & 0x3;
			cpaddr -= tail; /*sottraggo perchè sto scendendo nello stack*/
		}
		result = copyout((const void *)kargs[i], (userptr_t)cpaddr, length); /*copio da kernel a user*/
		if (result) {
			//kprintf("DAJEEE\n");
			kfree_all(kargs);
			kfree(kargs);
			kfree(kprogram);
		}
			
		*(uargs + i) = (char *)cpaddr; /*mi salvo l'indiritto dello stack user in uargs*/
		kargs[i] = NULL; 
		cpaddr -= sizeof(char *)*(i+1); /*sposto lo stack di un offset pari al
						numero di stringhe che ha messo in kargs[]*/
	}
	
	for (i = 0; i < (int)dim_args; i++) {
		result = copyout((const void *)uargs[i], (userptr_t)cpaddr, sizeof(char *)*(i+1));
		if (result) {
			kfree_all(kargs);
			kfree(kargs);
			kfree(kprogram);
		}
	}
		
	

	kfree_all(kargs);
	kfree(kargs);
	kfree(kprogram);
	/* Warp to user mode. */
	if (cpaddr % 8 == 0) {
		stackptr = cpaddr - 8;
	}
	else {
		stackptr = cpaddr - 4;
	}
	
	kprintf("Proc %s pid = %d, address = %p, father = %p\n", curproc->p_name, curproc->p_pid, curproc, curproc->p_parent);
	enter_new_process(dim_args /*argc*/, (userptr_t)cpaddr/*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  (vaddr_t)stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
	
	
	return 0;
}
#endif
