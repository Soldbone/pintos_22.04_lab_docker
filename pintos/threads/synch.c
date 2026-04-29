/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
   */

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static bool
cmp_priority (const struct list_elem *t1, const struct list_elem *t2,
						void *aux UNUSED)
{
	const struct thread *a = list_entry(t1, struct thread, elem);
	const struct thread *b = list_entry(t2, struct thread, elem);
	return a->priority > b->priority; // TODO: local ticks 비교하는 코드 짜기
}

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
   decrement it.

   - up or "V": increment the value (and wake up one waiting
   thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) {
	ASSERT (sema != NULL);

	sema->value = value;
	list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. This is
   sema_down function. */
void
sema_down (struct semaphore *sema) {
	enum intr_level old_level;

	ASSERT (sema != NULL);
	ASSERT (!intr_context ());

	old_level = intr_disable ();
	while (sema->value == 0) { // 왜 if 아니고 while 문? 어짜피 sema 하나만 들어오잖아 
		// waiters 리스트에 priority 순서대로 스레드 삽입 
		// list_push_back (&sema->waiters, &thread_current ()->elem);
		list_insert_ordered(&sema->waiters, &thread_current ()->elem, cmp_priority, NULL); 
		thread_block ();
	}
	sema->value--;
	intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) {
	enum intr_level old_level;
	bool success;

	ASSERT (sema != NULL);

	old_level = intr_disable ();
	if (sema->value > 0)
	{
		sema->value--;
		success = true;
	}
	else
		success = false;
	intr_set_level (old_level);

	return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) {
	enum intr_level old_level;
	struct thread *cur = thread_current(); 
	ASSERT (sema != NULL);

	old_level = intr_disable ();
	struct thread *first = NULL;
	if (!list_empty (&sema->waiters))  {
		first = list_entry (list_pop_front (&sema->waiters), struct thread, elem);
		thread_unblock (first);
	}
	sema->value++; //인터럽트 꺼뒀기 때문에 unblock 아래에서 sema 증가시켜도 괜찮음 

	// 궁금한바... 새로 뽑은 스레드가 현재보다 priority 높으면 바로 실행된댔는데, 바로 실행된다는게... sema value조차 올리지 않고, 인터럽트도 원래상태로 안돌려놓고 ㄹㅇ 바로. 바로 실행되는건지 or 이정도 작업은 다 한다음에 CPU를 양보해주는건지 
	// 양보해주고 다시 돌아오나? 

	// -> 정말 즉시 컨텍스트 스위치되는지 vs 정리 후 양보하는지 
	// -> 그 자리에서 즉시 스위치되는 것은 아니고 sema_up()의 핵심 작업을 끝낸 뒤 안전한 시점에 CPU를 넘김 
	

	// priority 순서대로 waiters 리스트 정렬 
	// list_sort(&sema->waiters, cmp_priority, NULL);

	intr_set_level (old_level);

	if (first != NULL && !intr_context () && first->priority > cur->priority) {
		thread_yield ();
	}
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) {
	struct semaphore sema[2];
	int i;

	printf ("Testing semaphores...");
	sema_init (&sema[0], 0);
	sema_init (&sema[1], 0);
	thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
	for (i = 0; i < 10; i++)
	{
		sema_up (&sema[0]);
		sema_down (&sema[1]);
	}
	printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) {
	struct semaphore *sema = sema_;
	int i;

	for (i = 0; i < 10; i++)
	{
		sema_down (&sema[0]);
		sema_up (&sema[1]);
	}
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock) {
	ASSERT (lock != NULL);

	lock->holder = NULL;
	sema_init (&lock->semaphore, 1);
}

static bool
cmp_lock_priority (const struct list_elem *t1, const struct list_elem *t2,
						void *aux UNUSED)
{
	const struct thread *a = list_entry(t1, struct thread, d_elem);
	const struct thread *b = list_entry(t2, struct thread, d_elem);
	return a->priority > b->priority; // TODO: local ticks 비교하는 코드 짜기
}


/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (!lock_held_by_current_thread (lock));


	struct thread *cur = thread_current ();

	// 스케줄링 기준: priority
	// 기부 받는 priority는 실제 priority에 반영되어야 함
	// 그럼 이전 값을 저장해야 됨 -> 어디에다가? donated_priority에? 그럼 뭐 되긴 하는데 변수 이름이 좀 이상함
	// origin_priority를 또 둬! 이것도 방법인데 그럴 바에 바꾸는 게 나음. 머리는 이게 덜 쓰긴 하는데 너무 더러움

	// donated_priority는 origin_priority로 바꾸고
	// priority 자체에 기부받는 값들을 넣으면 된다!

	// 전체 TODO 
	// 락을 쓸 수 없을 때 현재 스레드의 wait_on_lock에 lock의 address 저장
	if (lock->holder != NULL) {
		cur->wait_on_lock = lock;

		// priority 기부해야되면 기부
		struct thread *holder_t = lock->holder;
		if (holder_t->priority < cur->priority) {
			list_insert_ordered(&holder_t->donations, &cur->d_elem, cmp_lock_priority, NULL);
			// 추가하고 그중에 내가 제일크면 기부 (리스트의 첫번째 d_elem과 현재 스레드의 d_elem을 비교해서 같으면 현재 스레드의 우선순위가 가장 크다)
			// 현재 스레드의 우선순위를 lock 주인에게 기부해줌
			// 문제) 이 락 요청하는 애도 누군가한테 기부 받았으면 어떡함?
			// if (cur->donated_priority != -1) {
			// 	holder_t->donated_priority = cur->donated_priority; // 이렇게 priority를 바꿔주기만 하면 혼자 알아서 락 계속 갖고 있으면서 작업 끝까지 한다고? 그리고 알아서 cur은 락을 획득한다고? 
			// }
			// else {
			// 	holder_t->donated_priority = cur->priority;
			// }
			holder_t->priority = cur->priority;
		}
	}

	// 현재 스레드가 락을 얻은 경우에 현재 스레드의 donations 리스트에서 우선순위 기부해줍쇼 해야 하나? -> 아닌 것 같다. 위에서 이미 기부해줄게! 했다.
	// 난 락 주인보다도 작으니까... 할 수 잇는게 없어 -> 그럼 block == lock에 있는 semaphore의 waiters 리스트에 넣는다. (sema 값이 0인 경우에)
	sema_down (&lock->semaphore);
	lock->holder = thread_current ();
	cur->wait_on_lock = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock) {
	bool success;

	ASSERT (lock != NULL);
	ASSERT (!lock_held_by_current_thread (lock));

	success = sema_try_down (&lock->semaphore);
	if (success)
		lock->holder = thread_current ();
	return success;
}

/* Releases LOCK, which must be owned by the current thread.
   This is lock_release function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) {
	ASSERT (lock != NULL);
	ASSERT (lock_held_by_current_thread (lock));

	
	// 락이 제거되면, 현재 스레드가 제거한 락을 원하던 모든 스레드들을 donations 리스트에서 제거하고, donated_priority를 원래 priority로 설정 (제거한 애들 wait_on_lock도 적절히 변경해줄 것) -> 윤지가 잘 이해 못해서 그냥 받아썼다고 했음
	// (1) donations가 남아 있으면 첫번째 우선순위로 설정 (2) donations가 비어 있으면 -1로 설정


	// donation 리스트를 돌아? 어떻게 도는가. 
	// struct thread *cur = thread_current ();
	struct thread *holder_t = lock->holder;
	struct list_elem *e; // d_elem
	for (e = list_begin(&lock->holder->donations); e != list_end(&lock->holder->donations); e = list_next(e))  { // donations empty 예외처리를 해야되나? -> 윤지답은 안해도될 것 같다... 지섭답은 해야될 것 같다... 
    	if (list_entry(e, struct thread, d_elem)->wait_on_lock == lock) {
			// list_entry(e, struct thread, d_elem)->wait_on_lock = NULL; // 이거 필요 없는 듯. lock_acquire()에서 처리해줌.
			list_remove(e);
		}
	}
	
	if (list_empty(&lock->holder->donations)) {
		lock->holder->priority = lock->holder->origin_priority;
	} else {
		e = list_front(&lock->holder->donations);
		struct thread *d_thread = list_entry(e, struct thread, d_elem);
		lock->holder->priority = d_thread->priority;
	}

	// TODO 
	// 중첩기부에서 8단계 제한 
	lock->holder = NULL;
	sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) {
	ASSERT (lock != NULL);

	return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem {
	struct list_elem elem;              /* List element. */
	struct semaphore semaphore;         /* This semaphore. */
	struct thread *t;
};

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond) {
	ASSERT (cond != NULL);

	list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */

static bool cmp_sema_elem (const struct list_elem *waiter1, const struct list_elem *waiter2,
	void *aux UNUSED)
{
	const struct semaphore_elem *a = list_entry (waiter1, struct semaphore_elem, elem);
	const struct semaphore_elem *b = list_entry (waiter2, struct semaphore_elem, elem);
	return a->t->priority > b->t->priority; 
}

void
cond_wait (struct condition *cond, struct lock *lock) {
	struct semaphore_elem waiter;

	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	sema_init (&waiter.semaphore, 0);
	struct thread *cur = thread_current();
	waiter.t = cur;
	waiter.elem = cur->elem;

	// list_push_back (&cond->waiters, &waiter.elem);
	list_insert_ordered(&cond->waiters, &waiter.elem, cmp_sema_elem, NULL);

	lock_release (lock);
	sema_down (&waiter.semaphore);
	lock_acquire (lock);

	// condition 비교를 한다. 
	// 안맞으면 다시 cond_wait으로 들어간다. -> condition 조건을 만족했으니 깨어난 것 아닌가? 
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);
	ASSERT (!intr_context ());
	ASSERT (lock_held_by_current_thread (lock));

	if (!list_empty (&cond->waiters))
		sema_up (&list_entry (list_pop_front (&cond->waiters), struct semaphore_elem, elem)->semaphore);
	// 세마랑 cond 굴레에 빠졋삼. 

}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) {
	ASSERT (cond != NULL);
	ASSERT (lock != NULL);

	while (!list_empty (&cond->waiters))
		cond_signal (cond, lock);
}
