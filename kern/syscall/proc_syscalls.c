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
int sys_execv(char *program, char **args) {
	
	struct addrspace *as_prova;
	//struct addrspace *as_user;
	//struct addrspace *as_kernel;
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
	char *prova = args[0];
	KASSERT (prova != NULL);
	for (dim_args = 0; args[dim_args] != NULL; dim_args++);
	
	/*alloco il vettore*/
	kargs = (char **)kmalloc(dim_args * sizeof(char**));
	if (kargs == NULL) {
		/*TODO: libera memoria*/
		return ENOMEM;
	}
	
	/*alloco ogni riga del vettore*/
	for (i = 0; i < (int)dim_args; i++) {
		len = strlen(args[i]) + 1;
		kargs[i] = kmalloc(len); /*con terminatore di stringa*/
		if (kargs[i] == NULL) {
			/*TODO: libera memoria*/
			return ENOMEM;
		}
		/*una volta allocato l'elemento i-esimo del vettore facico copyin da user a kernel*/
		result = copyin((const_userptr_t)args[i], (void *)kargs[i], len);
		if (result) {
			/*TODO: libera memoria*/
			return -result;
		}
		/*debug*/
		kprintf("kargs[%d] = %s\n", i, kargs[i]);
	}
	as_prova = curproc->p_addrspace;
	
	/*faccio la stessa cosa col nome del programma*/
	len = strlen(program) + 1;
	kprogram = kmalloc(len);
	if (kprogram == NULL) {
		/*TODO: libera memoria*/
		return ENOMEM;
	}
	result = copyin((const_userptr_t)program, (void *)kprogram, len);
	if (result) {
		/*TODO: libera memoria*/
		return -result;
	}
	/*debug*/
	kprintf("kprogram = %s\n", kprogram);
	
	/*non mi serve più l'address space user, passo a quello di kernel*/
	
	/*t_as lo devo aggiungere io(?)*/
	as_destroy(curproc->p_addrspace); /* old addrspace is no longer needed */
	//as_prova = curproc->p_addrspace;
	curproc->p_addrspace = as_create(); //ricrea l'as
	if (curproc->p_addrspace==NULL) {
		return ENOMEM;
	}
	//as_prova = curproc->p_addrspace;
	KASSERT(as_prova != NULL);
	/* Open the file. */
	result = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (result) {
		return -result;
	}
	/* Switch to it and activate it. */
	proc_setas(curproc->p_addrspace);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/*TODO: libera memoria*/
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
	
	/*devo tornare da kernel a user*/
	/*strutture user dopo kernel*/
	char **uargs;
	unsigned int cpaddr;
	int length, tail;
	
	//userptr_t args_out_addr;
	uargs = (char **)kmalloc(sizeof(char *) * (dim_args + 1));
	if (uargs == NULL) {
		/*TODO: free*/
		return ENOMEM;
	}
	cpaddr = stackptr;
	for (i = 0; i < (int)dim_args; i++) {
		length = strlen(kargs[i])+1;
		cpaddr -= length;
		tail = 0;
		if (cpaddr & 0x3) {
			tail = cpaddr & 0x3;
			cpaddr -= tail;
		}
		copyout((const void *)kargs[i], (userptr_t)cpaddr, length); /*copio da kernel a user*/
		kargs[i] = NULL;
		cpaddr -= sizeof(char *)*(i+1); /*sposto lo stack di un offset pari al
						numero di stringhe che ha messo in kargs[]*/
		
		copyout((const void *)uargs, (userptr_t)cpaddr, sizeof(char *)*(i+1));
		
		/*TODO: free all*/
	}
	
	
	/* Warp to user mode. */
	

	enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
	
	kprintf("YAAAAAAAAAAAAAAAAAAY\n");
	
	return 0;
}
#endif
