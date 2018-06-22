/* This file will contain your solution. Modify it as you wish. */
#include <types.h>
#include <synch.h> 
#include <lib.h>
#include "producerconsumer_driver.h"

/* Declare any variables you need here to keep track of and
   synchronise your bounded. A sample declaration of a buffer is shown
   below. You can change this if you choose another implementation. */

static struct pc_data buffer[BUFFER_SIZE];
static int buffer_head = 0;
static int buffer_tail = 0;
static struct lock *buffer_lock;
static struct semaphore *safe_consumer;
static struct semaphore *safe_producer;


/* consumer_receive() is called by a consumer to request more data. It
   should block on a sync primitive if no data is available in your
   buffer. */

struct pc_data consumer_receive(void)
{
        struct pc_data thedata;
        P(safe_consumer);
        lock_acquire(buffer_lock);
        thedata = buffer[buffer_head];
        ++buffer_head;
        buffer_head %= BUFFER_SIZE;
        lock_release(buffer_lock);
        V(safe_producer);

        return thedata;
}

/* procucer_send() is called by a producer to store data in your
   bounded buffer. */

void producer_send(struct pc_data item)
{
        P(safe_producer);
        lock_acquire(buffer_lock);
        buffer[buffer_tail] = item;
        ++buffer_tail;
        buffer_tail %= BUFFER_SIZE;
        lock_release(buffer_lock);
        V(safe_consumer);
}




/* Perform any initialisation (e.g. of global data) you need
   here. Note: You can panic if any allocation fails during setup */

void producerconsumer_startup(void)
{
        buffer_lock = lock_create("buffer_lock");
        if (buffer_lock == NULL) {
                panic("producerconsumer: lock create failed");
        }
        safe_producer = sem_create("safe_producer", BUFFER_SIZE);
        if (safe_producer == NULL) {
                panic("producerconsumer: sem create failed");
        }
        safe_consumer = sem_create("safe_consumer", 0);
        if (safe_consumer== NULL) {
                panic("producerconsumer: sem create failed");
        }
}

/* Perform any clean-up you need here */
void producerconsumer_shutdown(void)
{
        lock_destroy(buffer_lock);
        sem_destroy(safe_producer);
        sem_destroy(safe_consumer);
}

