#include <types.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <lib.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <syscall.h>
#include <test.h>
#include <copyinout.h>

#include "opt-file.h"
#include "opt-execv.h"




#if OPT_EXECV

/*SDP OS161 PROJECT*/


static
void kfree_args(void **args)
{
	/*this function frees all the string arrays allocated during execv*/
	int i = 0;
	while (args[i] != NULL)
	{
		args[i] = NULL;
		kfree(args[i]);
		i++;
	}
	kfree(args[i]); //NULL value
	kfree(args);
	return;
}

/***************************************SYS_EXECV***************************************************/
int sys_execv(char *program, char **args)
{
	
	struct vnode *v;
	vaddr_t entrypoint, stackptr;
	int result;
	size_t argc, len, waste;
	int i;

	/*strutture kernel*/
	char *kprogram;
	char **kargs;

	struct addrspace *oldas;
	struct addrspace *newas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL); //warning

	/*verifico che entrambi gli argomenti passati ad execv siano puntatori validi*/
	if (program == NULL || args == NULL)
	{
		return EFAULT;
	}

	/*trovo la dimensione del vettore: ciclo finchè non trovo un puntatore a NULL*/

	for (argc = 0; args[argc] != NULL; argc++)
		;

	if (argc >= ARG_MAX)
	{
		return E2BIG;
	}

	/*alloco il vettore*/
	kargs = (char **)kmalloc(argc * sizeof(char **));
	if (kargs == NULL)
	{
		return ENOMEM;
	}

	/*alloco ogni riga del vettore*/
	for (i = 0; i < (int)argc; i++)
	{
		len = strlen(args[i]) + 1;
		kargs[i] = (char *)kmalloc(len * sizeof(char *));
		if (kargs[i] == NULL)
		{
			kfree_args((void **)kargs);
			return ENOMEM;
		}
		/*una volta allocato l'elemento i-esimo del vettore facico copyin da user a kernel*/
		result = copyinstr((const_userptr_t)args[i], kargs[i], len, &waste);
		if (result)
		{
			kfree_args((void **)kargs);
			return -result;
		}
	}

	kargs[i] = NULL;
	/*faccio la stessa cosa col nome del programma*/
	len = strlen(program) + 1;
	kprogram = kmalloc(len);
	if (kprogram == NULL)
	{
		kfree(kprogram);
		return ENOMEM;
	}
	result = copyinstr((const_userptr_t)program, kprogram, len, &waste);
	if (result)
	{
		kfree_args((void **)kargs);
		kfree(kprogram);
		return -result;
	}
	/* Open the file. */
	result = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (result)
	{
		as_destroy(curproc->p_addrspace);
		kfree_args((void **)kargs);
		kfree(kprogram);
		return -result;
	}
	/*address space handling*/

	/*create a new address space*/
	newas = as_create();
	if (newas == NULL)
	{
		vfs_close(v);
		return ENOMEM;
	}

	/*Switch to it and activate it*/
	oldas = proc_setas(newas); //proc_setas ritorna il vecchio as
	as_activate();

	/*Load the executable*/
	result = load_elf(v, &entrypoint);
	if (result)
	{
		/*p_addrspace will go away when curproc is destroyed*/
		vfs_close(v);

		return result;
	}

	/* Done with the file now. */
	vfs_close(v);
	/* Define the user stack in the address space */
	result = as_define_stack(newas, &stackptr);
	if (result)
	{
		/* p_addrspace will go away when curproc is destroyed */
		return -result;
	}

	/*devo tornare da kernel a user: qui devo usare il padding*/
	/*strutture user dopo kernel*/
	char **uargs;
	int length;

	uargs = (char **)kmalloc(sizeof(char *) * (argc + 1));
	if (uargs == NULL)
	{
		as_destroy(curproc->p_addrspace);
		kfree_args((void **)kargs);
		kfree(kprogram);

		return ENOMEM;
	}
	for (i = 0; i < (int)argc; i++)
	{
		length = strlen(kargs[i]) + 1;
		uargs[i] = (char *)kmalloc(length * sizeof(char *));
		stackptr -= length;
		/*padding*/
		if (stackptr & 0x3) stackptr -= stackptr & 0x3; //alignment by 4
		result = copyoutstr(kargs[i], (userptr_t)stackptr, length, &waste); /*copio da kernel a user*/
		if (result)
		{
			kfree_args((void **)kargs);
			kfree(kprogram);
			return -result;
		}

		uargs[i] = (char *)stackptr; /*mi salvo l'indiritto dello stack user in uargs*/
	}

	for (i = 0; i < (int)argc; i++)
	{
		result = copyout((const void *)uargs[argc - (i + 1)], (userptr_t)stackptr, sizeof(char *));
		if (result)
		{
			kfree_args((void **)kargs);
			kfree(kprogram);
			return -result;
		}
	}

	uargs[i] = NULL;
	as_destroy(oldas);

	kfree_args((void **)kargs);
	kfree(kprogram);
	kfree_args((void **)uargs); //non funziona perchè sono indirizzi user e si blocca su KASSERT di kfree perchè non è multiplo di pagina
	/* Warp to user mode. */
	enter_new_process(argc /*argc*/, (userptr_t)stackptr /*userspace addr of argv*/,
					  NULL /*userspace addr of environment*/,
					  stackptr, entrypoint);

	/* enter_new_process does not return. */
	panic("enter_new_process returned\n");
	return EINVAL;
	return 0;
}
#endif

