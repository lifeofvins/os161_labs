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


#define PRINT 0
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

/*PROGETTO OS161*/
void 
kfree_all(char *argv[]) {
	int i;

	for (i=0; argv[i]; i++)
		kfree(argv[i]);

}

/*PROGETTO OS161*/

int sys_execv(char *program, char **args) {

	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	size_t argc, len;
	int i;
	
	/*strutture kernel*/
	char *kprogram;
	char **kargs;
	
	struct addrspace *oldas;
	struct addrspace *newas;
	struct proc *proc = curproc;
	
	
	/*verifico che entrambi gli argomenti passati ad execv siano puntatori validi*/
	if (program == NULL || args == NULL) {
		return EFAULT;
	}
	
	/*trovo la dimensione del vettore: ciclo finchè non trovo un puntatore a NULL*/

	for (argc = 0; args[argc] != NULL; argc++);
	
	/*alloco il vettore*/
	kargs = (char **)kmalloc(argc * sizeof(char**));
	if (kargs == NULL) {
		return ENOMEM;
	}
	
	/*alloco ogni riga del vettore*/
	for (i = 0; i < (int)argc; i++) {
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
	/* Open the file. */
	result = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (result) {
		as_destroy(curproc->p_addrspace);
		kfree_all(kargs);
		kfree(kargs);
		kfree(kprogram);
		return -result;
	}
	/*address space handling*/
	
	
	/*create a new address space*/
	as_destroy(proc->p_addrspace);
	newas = as_create();
	if (newas == NULL) {
		vfs_close(v);
		return ENOMEM;
	}
	
	/*Switch to it and activate it*/
	oldas = proc_setas(newas);
	as_activate();
	
	/*Load the executable*/
	result = load_elf(v, &entrypoint);
	if  (result) {
		/*p_addrspace will go away when curproc is destroyed*/
		vfs_close(v);
		return result;
	}
	
	(void)oldas; //per evitare i warning
	
/*SO FAR SO GOOD*/
/*a questo punto del codice ci arrivano tutti i processi figli creati dalle fork chiamate prima della execv in testbin/farm*/
/*se sposto più in giù nel codice questa kprintf non ci arrivano tutti i figli*/
#if PRINT
	kprintf("Proc %s pid = %d, address = %p, father = %p\n", curproc->p_name, curproc->p_pid, curproc, curproc->p_parent);
#endif

	/* Done with the file now. */
	vfs_close(v);
	/* Define the user stack in the address space */
	result = as_define_stack(newas, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		return -result;
	}
	
	/*devo tornare da kernel a user: qui devo usare il padding*/
	/*strutture user dopo kernel*/
	char **uargs;
	unsigned int ustackptr; /*user stack pointer*/
	int length, tail;
	

	uargs = (char **)kmalloc(sizeof(char *) * (argc + 1));
	if (uargs == NULL) {
		as_destroy(curproc->p_addrspace);
		kfree_all(kargs);
		kfree(kargs);
		kfree(kprogram);
		return ENOMEM;
	}
	ustackptr = stackptr;
	for (i = 0; i < (int)argc; i++) {
		length = strlen(kargs[i])+1;
		ustackptr -= length;
		tail = 0;
		/*padding*/
		if (ustackptr & 0x3) {
			tail = ustackptr & 0x3;
			ustackptr -= tail; /*sottraggo perchè sto scendendo nello stack*/
		}
		result = copyout((const void *)kargs[i], (userptr_t)ustackptr, length); /*copio da kernel a user*/
		if (result) {
			kfree_all(kargs);
			kfree(kargs);
			kfree(kprogram);
		}
			
		*(uargs + i) = (char *)ustackptr; /*mi salvo l'indiritto dello stack user in uargs*/
		kargs[i] = NULL; 
		ustackptr -= sizeof(char *)*(i+1); /*sposto lo stack di un offset pari al
						numero di stringhe che ha messo in kargs[]*/
	}
	
	for (i = 0; i < (int)argc; i++) {
		result = copyout((const void *)uargs[i], (userptr_t)ustackptr, sizeof(char *)*(i+1));
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

	if (ustackptr % 8 == 0) {
		stackptr = ustackptr - 8;
	}
	else {
		stackptr = ustackptr - 4;
	}
	enter_new_process(argc /*argc*/, (userptr_t)ustackptr/*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  (vaddr_t)stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
	return 0;
}
#endif
