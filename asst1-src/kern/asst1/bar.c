#include <types.h>
#include <lib.h>
#include <synch.h>
#include <test.h>
#include <thread.h>

#include "bar.h"
#include "bar_driver.h"


/*
 * **********************************************************************
 * YOU ARE FREE TO CHANGE THIS FILE BELOW THIS POINT AS YOU SEE FIT
 *
 */

/* Declare any globals you need here (e.g. locks, etc...) */

#define BUFFER_SIZE NCUSTOMERS

static struct semaphore *safe_consumer;
static struct semaphore *safe_producer;
static struct semaphore *bottles[NBOTTLES];
static struct semaphore *customer[NCUSTOMERS];
static struct semaphore *staff[NBARTENDERS];
static struct barorder *order_buffer[BUFFER_SIZE];
static struct lock *buffer_lock;
static struct lock *bottles_lock;
volatile unsigned long int buffer_counter;
volatile unsigned long int consume_counter;


/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY CUSTOMER THREADS
 * **********************************************************************
 */

/*
 * order_drink()
 *
 * Takes one argument referring to the order to be filled. The
 * function makes the order available to staff threads and then blocks
 * until a bartender has filled the glass with the appropriate drinks.
 */

void order_drink(struct barorder *order)
{
		//Ensure the order on more than NCUSTOMERS
		P(safe_producer);
		//Lock order buffer 
		lock_acquire(buffer_lock);
		//Track who send this order
		order->id = buffer_counter % NCUSTOMERS;
        order_buffer[buffer_counter++ % BUFFER_SIZE] = order;
        lock_release(buffer_lock);
        //Wake up BARTENDER to take order
        V(safe_consumer);
        //Wait until the order has been served
        P(customer[order->id]);
}

/*
 * **********************************************************************
 * FUNCTIONS EXECUTED BY BARTENDER THREADS
 * **********************************************************************
 */

/*
 * take_order()
 *
 * This function waits for a new order to be submitted by
 * customers. When submitted, it returns a pointer to the order.
 *
 */

struct barorder *take_order(void)
{
		//Ensure there at least one order
		P(safe_consumer);
		lock_acquire(buffer_lock);
		//Track who take this order
		order_buffer[consume_counter % BUFFER_SIZE]->staff = consume_counter % NBARTENDERS;
        struct barorder *ret = order_buffer[consume_counter++ % BUFFER_SIZE];
        lock_release(buffer_lock);
        return ret;
}


/*
 * fill_order()
 *
 * This function takes an order provided by take_order and fills the
 * order using the mix() function to mix the drink.
 *
 * NOTE: IT NEEDS TO ENSURE THAT MIX HAS EXCLUSIVE ACCESS TO THE
 * REQUIRED BOTTLES (AND, IDEALLY, ONLY THE BOTTLES) IT NEEDS TO USE TO
 * FILL THE ORDER.
 */

void fill_order(struct barorder *order)
{

        /* add any sync primitives you need to ensure mutual exclusion
           holds as described */

        /* the call to mix must remain */
		//only one staff can access bottles at the same time
	    P(staff[order->staff]);
		int i;        
		for(i = 0; i < DRINK_COMPLEXITY; ++i){
			if(order->requested_bottles[i]) P(bottles[order->requested_bottles[i]-1]);
		}
		mix(order);
		for(i = 0; i < DRINK_COMPLEXITY; ++i){
      		if(order->requested_bottles[i]) V(bottles[order->requested_bottles[i]-1]);
      	}
      	//Wake up next staff to use bottles
      	//Ensure there no starvation because the orders is filled in the order of the orders sent
      	V(staff[(order->staff+1)%NBARTENDERS]);
			
}

/*
 * serve_order()
 *
 * Takes a filled order and makes it available to (unblocks) the
 * waiting customer.
 */

void serve_order(struct barorder *order)
{
		//wake up customer to send another order once they druck up
      	V(safe_producer);
      	//Now customer can enjoy their order
      	V(customer[order->id]);
}


/*
 * **********************************************************************
 * INITIALISATION AND CLEANUP FUNCTIONS
 * **********************************************************************
 */


/*
 * bar_open()
 *
 * Perform any initialisation you need prior to opening the bar to
 * bartenders and customers. Typically, allocation and initialisation of
 * synch primitive and variable.
 */

void bar_open(void)
{

	buffer_counter = 0;
	consume_counter = 0;
	safe_consumer = sem_create("consumer", 0);

	if (safe_consumer == NULL) panic("bar: sem create failed");

	safe_producer = sem_create("producer", NCUSTOMERS);

	if (safe_producer == NULL) panic("bar: sem create failed");
	
	buffer_lock = lock_create("bufferLock");

	if (buffer_lock == NULL) panic("bar: lock create failed");

	bottles_lock = lock_create("bottlesLock");

	if (bottles_lock == NULL) panic("bar: lock create failed");

	int i;
	for(i = 0; i < NBOTTLES; ++i){
		bottles[i] = sem_create("bottle", 1);
		if (bottles[i] == NULL) panic("bar: sem create failed");
	}
	for(i = 0; i < NCUSTOMERS; ++i){
		customer[i] = sem_create("customer", 0);
		if (customer[i] == NULL) panic("bar: sem create failed");
    }

    staff[0] = sem_create("staff", 1);

	for(i = 0; i < NBARTENDERS; ++i){
		if(i) staff[i] = sem_create("staff", 0);
		if(staff[i] == NULL) panic("bar: sem create failed");   
	}
}

/*
 * bar_close()
 *
 * Perform any cleanup after the bar has closed and everybody
 * has gone home.
 */

void bar_close(void)
{
	int i;
	sem_destroy(safe_consumer);
	sem_destroy(safe_producer);
	lock_destroy(bottles_lock);
	lock_destroy(buffer_lock);
	for(i = 0; i < NBOTTLES; ++i){
		sem_destroy(bottles[i]);
	}
	for(i = 0; i < NCUSTOMERS; ++i){
		sem_destroy(customer[i]);
	}
	for(i = 0; i < NBARTENDERS; ++i){
		sem_destroy(staff[i]);
	}
	
}


