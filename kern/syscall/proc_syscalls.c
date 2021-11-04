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


#define PRINT 1
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
#if PRINT
kprintf("I'm the father pid = %d address = %p\n", parent->p_pid, parent);
#endif
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
	size_t len = (size_t)strlen(arg) + 1;
	//size_t padding = (size_t)strlen(arg) + 1;
	size_t padding = 0;
	kprintf("Stringa: %s\n", arg);
	while((len + padding) % 4 != 0)
		padding++;
		
	//padding -= strlen(arg);
#if PRINT
	kprintf("Padding necessario: %d caratteri\n", padding);
#endif
	/*adesso so quanto padding mettere*/
	return padding;
}

void add_padding(char *arg, int index, int padding) {
	int i;
	for (i = index; i < index + padding; i++) {
		arg[i] = '\0';
	}
#if PRINT
	kprintf("Padded arg = %s\n", arg);
#endif
}
	
	
int copyArgs (int argc, char **argv, userptr_t *argvAddr, vaddr_t *stackptr) {
	vaddr_t stack = *stackptr; 
	char **newArgv = kmalloc(sizeof(char *)*(argc+1));
	size_t wasteOfSpace;
	int errcode;
	
	for (int i = 0; i < argc; ++i) {
		int arglen = strlen(*(argv + i)) + 1; //length of char array for this arg
		stack -= ROUNDUP(arglen, 8);
		errcode = copyoutstr(*(argv+i), (userptr_t)stack, arglen, &wasteOfSpace);
		if (errcode) {
			kfree(newArgv);
			return errcode;
		}
		*(newArgv + i) = (char *)stack; //our argv kernel array is going to contain the user as
	}
	*(newArgv + argc) = NULL; //set final address to NULL
	
	for (int i = 0; i <= argc; ++i) {
		stack -= sizeof(char *); //move the stack pointer back one pointer worth of space
		errcode = copyout(newArgv + (argc - i), (userptr_t)stack, sizeof(char *));
		if (errcode) {
			kfree(newArgv);
			return errcode;
		}
	}
	
	*argvAddr = (userptr_t)stack; //set the argv array in userland to start at where we put it
	if (stack % 8 == 0) stack -= 8;
	else stack -= 4;
	*stackptr = stack; //set the real stack pointer to the one we've been dealing with
	kfree(newArgv);
	return 0;
}














int sys_execv(char *program, char **args) {

	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	size_t argc, len, init_len, pad_len;
	int i;
	
	/*strutture kernel*/
	char *kprogram;
	char **kargs;
	
	
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
		/*debug*/
#if PRINT
		kprintf("kargs[%d] = %s\n", i, kargs[i]);
#endif
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
#if PRINT
	kprintf("kprogram = %s\n", kprogram);
#endif
	/*address space handling*/
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
	
	
	
	
	/*prima di passare alla parte user faccio il padding del buffer di kernel*/
	char **padded_kargs;
	/*alloco il vettore*/
	padded_kargs = (char **)kmalloc(argc * sizeof(char**));
	if (padded_kargs == NULL) {
		return ENOMEM;
	}
	
	/*alloco ogni riga del vettore*/
	for (i = 0; i < (int)argc; i++) {
		init_len = strlen(args[i] + 1);
		pad_len = padding_multiple_four(kargs[i]);
		len = init_len + pad_len;
		padded_kargs[i] = kmalloc(len);
		padded_kargs[i+1] = NULL;
		if (padded_kargs[i] == NULL) {
			kfree_all(padded_kargs);
			kfree(padded_kargs);
			return ENOMEM;
		}
		
		strcat(padded_kargs[i], kargs[i]);
		add_padding(kargs[i], init_len, pad_len);
		
		if (result) {
			kfree_all(kargs);
			kfree(kargs);
			return -result;
		}
	}
	
	/*strutture user dopo kernel*/
	char **uargs;
	unsigned int cpaddr;
	int length, tail;
	

	uargs = (char **)kmalloc(sizeof(char *) * (argc + 1));
	if (uargs == NULL) {
		as_destroy(curproc->p_addrspace);
		kfree_all(kargs);
		kfree(kargs);
		kfree(kprogram);
		return ENOMEM;
	}
	cpaddr = stackptr;
	for (i = 0; i < (int)argc; i++) {
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
	
	for (i = 0; i < (int)argc; i++) {
		result = copyout((const void *)uargs[i], (userptr_t)cpaddr, sizeof(char *)*(i+1));
		if (result) {
			//kprintf("DAJEEE\n");
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
#if PRINT
	kprintf("Proc %s pid = %d, address = %p, father = %p\n", curproc->p_name, curproc->p_pid, curproc, curproc->p_parent);
#endif
	enter_new_process(argc /*argc*/, (userptr_t)cpaddr/*userspace addr of argv*/,
			  NULL /*userspace addr of environment*/,
			  (vaddr_t)stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
	return 0;
}
#endif
