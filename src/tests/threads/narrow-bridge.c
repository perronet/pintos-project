/* Tests producer/consumer communication with different numbers of threads.
 * Automatic checks only catch severe problems like crashes.
 */

#include <stdio.h>
#include "tests/threads/tests.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "devices/timer.h"
#include "threads/thread.h"
#include "threads/synch.h"

#define LEFT 0
#define RIGHT 1

//UP and DOWN are used for semaphores
#define UP 0
#define DOWN 1

#define NORMAL 0
#define EMERGENCY 1

#define BRIDGE_CAPACITY 3

#define CREATED_MSG  	"| Created |          |          |           |        |"
#define ARRIVE_MSG   	"|         | Arriving |          |           |        |"
#define CROSSING_MSG 	"|         |          | Crossing |           |        |"
#define FINISHING_MSG 	"|         |          |          | Finishing |        |"
#define EXIT_MSG 	 	"|         |          |          |           | Exited |"


void narrow_bridge(unsigned int num_vehicles_left, unsigned int num_vehicles_right,
        unsigned int num_emergency_left, unsigned int num_emergency_right);
void OneVehicle(int direc, int prio);
void ArriveBridge(unsigned int direc, int prio);
void CrossBridge(unsigned int direc, int prio);
void ExitBridge(unsigned int direc, int prio);

static void vehicle_thread(void *aux);
static void waiter_sema(int vehicle_dir, int prio, unsigned int sem_dir);
static void log_vehicle(unsigned int direc, int prio, char * msg);

static struct semaphore mutex;
static struct semaphore waiters_ln;
static struct semaphore waiters_rn;
static struct semaphore waiters_le;
static struct semaphore waiters_re;

/* Waiters status */
static unsigned int waiting_ln;
static unsigned int waiting_rn;
static unsigned int waiting_le;
static unsigned int waiting_re;

/* Bridge status */
static unsigned int bridge_direction; //either LEFT or RIGHT
static unsigned int bridge_count;

void test_narrow_bridge(void)
{
    narrow_bridge(1, 1, 1, 1);
	/*
    narrow_bridge(0, 0, 0, 0);
    narrow_bridge(1, 0, 0, 0);
    narrow_bridge(0, 0, 0, 1);
    narrow_bridge(0, 4, 0, 0);
    narrow_bridge(0, 0, 4, 0);
    narrow_bridge(3, 3, 3, 3);
    narrow_bridge(4, 3, 4 ,3);
    narrow_bridge(7, 23, 17, 1);
    narrow_bridge(40, 30, 0, 0);
    narrow_bridge(30, 40, 0, 0);
    narrow_bridge(23, 23, 1, 11);
    narrow_bridge(22, 22, 10, 10);
    narrow_bridge(0, 0, 11, 12);
    narrow_bridge(0, 10, 0, 10);
    //*/
    //narrow_bridge(0, 10, 10, 0);
    //narrow_bridge(5, 0, 0, 1);

    timer_sleep(1000);
    pass();
}

void narrow_bridge(UNUSED unsigned int num_vehicles_left, UNUSED unsigned int num_vehicles_right,
        UNUSED unsigned int num_emergency_left, UNUSED unsigned int num_emergency_right)
{
	sema_init(&mutex, 1);
	sema_init(&waiters_ln, 0);
	sema_init(&waiters_rn, 0);
	sema_init(&waiters_le, 0);
	sema_init(&waiters_re, 0);
	bridge_direction = LEFT;
	bridge_count = 0;
	waiting_ln = waiting_rn = waiting_le = waiting_re = 0;

	for(unsigned int i = 0; i < num_vehicles_left; i++)
	{
		uintptr_t setup = (LEFT << 1) + NORMAL;
        thread_create("broom LN", PRI_DEFAULT, vehicle_thread, (void*)setup);
		log_vehicle(LEFT, NORMAL, CREATED_MSG);
	}

	for(unsigned int i = 0; i < num_vehicles_right; i++)
	{
		uintptr_t setup = (RIGHT << 1) + NORMAL;
        thread_create("broom RN", PRI_DEFAULT, vehicle_thread, (void*)setup);
		log_vehicle(RIGHT, NORMAL, CREATED_MSG);
	}

	for(unsigned int i = 0; i < num_emergency_left; i++)
	{
		uintptr_t setup = (LEFT << 1) + EMERGENCY;
        thread_create("broom LN", PRI_DEFAULT, vehicle_thread, (void*)setup);
		log_vehicle(LEFT, EMERGENCY, CREATED_MSG);
	}

	for(unsigned int i = 0; i < num_emergency_right; i++)
	{
		uintptr_t setup = (RIGHT << 1) + EMERGENCY;
        thread_create("broom RN", PRI_DEFAULT, vehicle_thread, (void*)setup);
		log_vehicle(RIGHT, EMERGENCY, CREATED_MSG);
	}
}

static void vehicle_thread(void *aux)
{
	//Extracting data from the bitmasked setup variable
	uintptr_t setup = (unsigned int)aux;
	unsigned int prio = setup & 1;
	unsigned int direction = (setup & 2) >> 1;

	OneVehicle(direction, prio);
}

void OneVehicle(int direc, int prio) 
{
  ArriveBridge(direc,prio);
  CrossBridge(direc,prio);
  ExitBridge(direc,prio);
}

void ArriveBridge(unsigned int direc, int prio)
{
	//ArriveBridge must not return (i.e., it blocks the thread) until it 
	//is safe for the car to cross the bridge in the given direction.
	log_vehicle(direc, prio, ARRIVE_MSG);
	
	bool is_emergency = prio == EMERGENCY;

	bool will_cross = false;

	sema_down(&mutex);
	while(!will_cross)
	{
		bool emergency_waiting = waiting_le + waiting_re > 0;
		bool allowed_to_pass = is_emergency || !emergency_waiting;
		bool bridge_empty = bridge_count == 0;
		bool bridge_available = bridge_direction == direc &&
									bridge_count < BRIDGE_CAPACITY;

		will_cross = allowed_to_pass && (bridge_empty || bridge_available);

		if(will_cross)
		{
			ASSERT(bridge_empty || bridge_direction == direc);
			bridge_direction = direc;

			bridge_count ++;
			ASSERT(bridge_count <= BRIDGE_CAPACITY);

			sema_up(&mutex);			
		}
		else
		{
			sema_up(&mutex);

			waiter_sema(direc, prio, DOWN);

			sema_down(&mutex);
		}
	}
}

void CrossBridge(unsigned int direc, int prio)
{
	log_vehicle(direc, prio, CROSSING_MSG);
	timer_sleep(100);
	log_vehicle(direc, prio, FINISHING_MSG);
}

void ExitBridge(unsigned int direc, int prio)
{
	//ExitBridge is called to indicate that the caller has finished crossing 
	//the bridge; ExitBridge should take steps to let additional cars cross 
	//the bridge (i.e., unblock them).
	
	sema_down(&mutex);
	
	ASSERT(bridge_count > 0);
	bridge_count --;

	bool bridge_empty = bridge_count == 0;
	if(bridge_empty)
	{
		if(waiting_le > 0) 		waiter_sema(LEFT, true, UP);
		else if(waiting_re > 0) waiter_sema(RIGHT, true, UP);
		else if(waiting_ln > 0) waiter_sema(LEFT, false, UP);
		else if(waiting_rn > 0) waiter_sema(RIGHT, false, UP);
	}
	else
	{//bridge is not empty
		if(bridge_direction == LEFT)
		{
			if(waiting_le > 0) 		waiter_sema(LEFT, true, UP);
			else if(waiting_re > 0) waiter_sema(RIGHT, true, UP);
		}
		else
		{//bridge direction == RIGHT
			if(waiting_ln > 0) 		waiter_sema(LEFT, false, UP);
			else if(waiting_rn > 0) waiter_sema(RIGHT, false, UP);
		}
	}

	sema_up(&mutex);

	log_vehicle(direc, prio, EXIT_MSG);
}

/* Execute up/down on the corresponding semaphore of the given vehicle type 
   and updates the waiters status */
static void waiter_sema(int vehicle_dir, int prio, unsigned int sem_dir)
{
	ASSERT(vehicle_dir == LEFT || vehicle_dir == RIGHT);
	ASSERT(prio == EMERGENCY || prio == NORMAL);
	ASSERT(sem_dir == UP || sem_dir == DOWN);

	/* if we put the vehicle to sleep, we will then have one waiting more 
	   vehicle. Otherwise, we will have one less waiting vehicle */
	int difference = sem_dir == DOWN ? 1 : -1; 

	struct semaphore * target_sema;

	if(vehicle_dir == LEFT)
	{	
		if(prio == EMERGENCY)
		{//LE
			waiting_le += difference;
			target_sema = &waiters_le;
		}
		else
		{//LN
			waiting_ln += difference;
			target_sema = &waiters_ln;
		}
	}
	else
	{//vehicle_dir == RIGHT
		if(prio == EMERGENCY)
		{//RE
			waiting_re += difference;
			target_sema = &waiters_re;
		}
		else
		{//RN
			waiting_rn += difference;
			target_sema = &waiters_rn;
		}
	}

	/*
	printf("sema waiter %s %s %s\n", (is_left? "L" : "R"), 
								(is_emergency? "E" : "N"), 
								(sem_dir == UP? "up" : "down"));
	//*/

	if(sem_dir == UP)
		sema_up(target_sema);
	else
		sema_down(target_sema);
}

static void log_vehicle(unsigned int direc, int prio, char * msg)
{
	printf("[%s %s] %s\n", 
		direc == LEFT ? "L" : "R", 
		prio == EMERGENCY ? "E" : "N",
		msg);
}

