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

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>


#define SEM 0
#define WCHAN 1
////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
        struct semaphore *sem;

        sem = kmalloc(sizeof(*sem));
        if (sem == NULL) {
                return NULL;
        }

        sem->sem_name = kstrdup(name);
        if (sem->sem_name == NULL) {
                kfree(sem);
                return NULL;
        }

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
        sem->sem_count = initial_count;

        return sem;
}

void
sem_destroy(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
        kfree(sem->sem_name);
        kfree(sem);
}

void
P(struct semaphore *sem)
{
        KASSERT(sem != NULL);

        /*
         * May not block in an interrupt handler.
         *
         * For robustness, always check, even if we can actually
         * complete the P without blocking.
         */
        KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock); /*lo spinlock protegge il contatore del semaforo*/

	while(sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
        }
        KASSERT(sem->sem_count > 0); /*se quest'espressione è falsa allora chiama panic*/
        sem->sem_count--;
        spinlock_release(&sem->sem_lock);
}

void
V(struct semaphore *sem)
{
        KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

        sem->sem_count++;
        KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.



/*LAB3: devo completare/cambiare le funzioni lock_create(), lock_destroy(), lock_acquire(), lock_release(), lock_do_i_hold()*/
/*************************************************SEMAFORO***************************/
#if SEM
struct lock *
lock_create(const char *name)
{
  struct lock *lock;

        lock = kmalloc(sizeof(*lock)); /*alloca la struct lock*/
        if (lock == NULL) {
                return NULL;
        }
	/*LAB3: creo il semaforo del lock (versione con semaforo)*/
        lock->lock_sem = sem_create("lock_sem", 1);


        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

	//HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);

        // add stuff here as needed

        

        return lock;
}

void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);

        // add stuff here as needed
        
        /*LAB3: distruggo anche il semaforo del lock e libero thread_who_acquired*/
        sem_destroy(lock->lock_sem);
        kfree(lock->thread_who_acquired);
        kfree(lock->lk_name);
                
        kfree(lock);

}

/*si mette void per evitare che il compilatore dia dei warning compilati come errori legati al fatto di avere un parametro o una variabile che sono dichiarati ma non utilizzati --> si scrive un'espressione con un solo dato, si fa il cast a void per dire che non la assegno a nessuna variabile*/
void
lock_acquire(struct lock *lock)
{
	/* Call this (atomically) before waiting for a lock */
	//HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

        // Write this

        //(void)lock;  // suppress warning until code gets written

	/* Call this (atomically) once the lock is acquired */
	//HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
	
	/*LAB3: implemento lock acquire utilizzando P(sem)*/

	P(lock->lock_sem); //versione con semaforo
	lock->thread_who_acquired = curthread;

}

void
lock_release(struct lock *lock)
{
	/* Call this (atomically) when the lock is released */
	//HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

        // Write this

        //(void)lock;  // suppress warning until code gets written
        /*LAB3: rilascio il semaforo solo se il thread è lo stesso che ha fatto lock_acquire*/
        KASSERT(lock_do_i_hold(lock)); /*bloccati se non c'è ownership*/

        V(lock->lock_sem); //versione con semaforo
        
}
bool
lock_do_i_hold(struct lock *lock)
{
/*funzione che ritorna vero se il thread possiede il lock*/
        // Write this

        //(void)lock;  // suppress warning until code gets written
  struct thread *current_thread = curthread;
  if (current_thread == lock->thread_who_acquired) {
	return true; // dummy until code gets written
  }
  else {
	return false;
  }
}
#endif
/*************************************************WCHAN E SPINLOCK***************************/
#if WCHAN
/*versione con wchan e spinlock*/
struct lock *
lock_create(const char *name)
{
  struct lock *lock;

        lock = kmalloc(sizeof(*lock)); /*alloca la struct lock*/
        if (lock == NULL) {
                return NULL;
        }
	

        lock->lk_name = kstrdup(name);
        if (lock->lk_name == NULL) {
                kfree(lock);
                return NULL;
        }

        // add stuff here as needed
        /*LAB3*/
        /*inizializzo spinlock e wait channel*/

        lock->lock_wchan = wchan_create(lock->lk_name);
        if (lock->lock_wchan == NULL) {
        	kfree(lock->lk_name);
        	kfree(lock);
        	return NULL;
        }
        
                /*inizialmente il lock è unlocked*/
        lock->locked = false;	
        /*inizializzo spinlock*/
        
	spinlock_init(&lock->lock_spinlock);
	
        

        return lock;
}

/*questa funzione non deve essere atomica*/
void
lock_destroy(struct lock *lock)
{
        KASSERT(lock != NULL);
        KASSERT(lock->locked == false);

        // add stuff here as needed
        
        /*distruggo spinlock e wait channel*/
        spinlock_cleanup(&lock->lock_spinlock);
        wchan_destroy(lock->lock_wchan);
        
        kfree(lock->lk_name);
                
        kfree(lock);

}



/*questa funzione deve essere atomica*/

void
lock_acquire(struct lock *lock)
{
	/* Call this (atomically) before waiting for a lock */
	//HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);

        // Write this

        //(void)lock;  // suppress warning until code gets written

	/* Call this (atomically) once the lock is acquired */
	//HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
	
	
		/*LAB3*/
	KASSERT(lock != NULL);
	KASSERT(curthread != NULL);
        //Ensure this operation is atomic
        spinlock_acquire(&lock->lock_spinlock);
        
        /*come nella P dei semafori, anche qui abbiamo un ciclo while in cui mettiamo la wchan_sleep,
        *solo che come condizione del while abbiamo lock->locked (flag) invece del contatore del semaforo*/
        
        while (lock->locked) {
        	            /*
                Lock the wait channel,
                just in case someone else is trying to
                acquire the lock at the same time
            */
	    wchan_sleep(lock->lock_wchan, &lock->lock_spinlock);

	}
	//Sanity Check - Make sure we're unlocked.
        KASSERT(lock->locked == false);
        //Lock the lock!
        lock->locked = true;
	lock->thread_who_acquired = curthread;
        //Now, release the spinlock.
        spinlock_release(&lock->lock_spinlock);
}


/*deve essere ATOMICA*/
void
lock_release(struct lock *lock)
{

	/* Call this (atomically) when the lock is released */
	//HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);

        // Write this

        //(void)lock;  // suppress warning until code gets written
        
        
        
        /*LAB3*/

        KASSERT(lock != NULL);
	KASSERT (curthread != NULL);
	
	/*per assicurarmi che l'operazione sia atomica acquisisco lo spinlock del lock*/
	spinlock_acquire(&lock->lock_spinlock);
	
	/*mi assicuro che il lock sia effettivamente bloccato*/
	KASSERT (lock->locked == true);
	/*mi assicuro che colui che rilascia il lock sia lo stesso che lo ha acquisito*/
	KASSERT (lock_do_i_hold(lock));
	
	/*sblocco il lock*/
	lock->locked = false;
	lock->thread_who_acquired = NULL;
	
	wchan_wakeone(lock->lock_wchan, &lock->lock_spinlock);
	
	/*fine dell'operazione atomica*/
	spinlock_release(&lock->lock_spinlock);
	
        
}
bool
lock_do_i_hold(struct lock *lock)
{
/*funzione che ritorna vero se il thread possiede il lock*/
        // Write this

        //(void)lock;  // suppress warning until code gets written
  struct thread *current_thread = curthread;
  if (current_thread == lock->thread_who_acquired) {
	return true; // dummy until code gets written
  }
  else {
	return false;
  }
}
#endif

////////////////////////////////////////////////////////////
//
// CV

struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		kfree(cv);
		return NULL;
	}

	// add stuff here as needed
	/*LAB3: creo wchan e lock associati alla cv*/
	cv->cv_wchan = wchan_create(cv->cv_name);
	if (cv->cv_wchan == NULL) {
		kfree(cv->cv_wchan);
		kfree(cv->cv_name);
		return NULL;
	}
	
	spinlock_init(&cv->cv_spinlock);

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	// add stuff here as needed
	/*libero spinlock e wchan*/
	kfree(cv->cv_wchan);
	spinlock_clean(&cv->cv_spinlock);
	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	// Write this
	//(void)cv;    // suppress warning until code gets written
	//(void)lock;  // suppress warning until code gets written
	
	/*LAB3: implemento cv_wait con wchan e spinlock*/
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	// Write this
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	// Write this
	(void)cv;    // suppress warning until code gets written
	(void)lock;  // suppress warning until code gets written
}
