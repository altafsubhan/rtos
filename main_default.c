/*
 * Default main.c for rtos lab.
 * @author Subhan and Susan, 2018
 */

#include <LPC17xx.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "context.h"

// for setting stack pointer bit
#define SPBIT 0x02

uint32_t msTicks = 0;
uint8_t bitVector = 0;

// triggers context switch every 1 ms
void SysTick_Handler(void) {
	msTicks++;
	SCB->ICSR |= (0x01 << 28); // triggering pendSV_Handler
}

enum priorities{ // priorities of different tasks
		idleDemon,
		low,
		belowNormal,
		normal,
		aTeeeeensyBitAmountAboveNormal,
		superDuperImportant
} priority;

enum states{ // states of the different tasks
		inactive,
		waiting,
		ready,
		running
} state;

typedef struct TCB { // task control block
	uint8_t taskID;
	uint32_t taskBase;
	uint32_t taskSP;
	
	uint8_t priority;
	uint8_t state;
	
	// pointer to next TCB in queue
	struct TCB * next;
} TCB_t;

typedef struct queue {	// implement queues for scheduler
	TCB_t * head;
	uint8_t size;
} queue_t;

queue_t priorityQ[6];	// queues for each priority level

void initializeQ() {	// for initializing each queue
		for (int i = 0; i < 6; i++){
			priorityQ[i].head = NULL;
			priorityQ[i].size = 0;
		}
}

// adds TCB node to end of queue
void enqueue(queue_t * list, TCB_t * toAdd) {
	if (list->size)	{	// if queue is not empty
		TCB_t * listNode = list->head;
		// find end of queue
		while (listNode->next != NULL) {
			listNode = listNode->next;
		}
		listNode->next = toAdd;	// add TCB to end
	} else {	// if queue is empty
		list->head = toAdd;		// add TCB to queue
	}
	bitVector |= 1<<toAdd->priority;  // update bit vector
	toAdd->next = NULL;
	list->size++;	// update size
}

TCB_t * dequeue(queue_t * list) {	// removes TCB from front of queue
	TCB_t * currNode = list->head;	// store first TCB
	list->head = list->head->next;	// update head
	list->size--;		// update size
	if (!list->size)	// update bit vector if queue becomes empty
		bitVector &= ~(1<<currNode->priority);
	return currNode;	// return saved TCB
}

// removes specific TCB from queue, used for priority inheritance in mutex
TCB_t * removeTCB(TCB_t * taskToDel) {
	// get the corresponding queue from task
	queue_t * list = &(priorityQ[taskToDel->priority]);
	TCB_t * temp;
	
	// if removing first TCB
	if (list->head->taskID == taskToDel->taskID){
		return dequeue(list);
	}

	// If removing middle  or last TCB
	TCB_t * iter = list->head;

	while (iter->next != NULL) {
		// find TCB in the queue
		if (iter->next->taskID == taskToDel->taskID) {
				temp = iter->next;	// store TCB to remove
				iter->next = iter->next->next;	// update queue
				list->size--;	// update size
				return temp;	// return stored TCB
		}
		iter = iter->next;	// iterate through list until TCB found
	}
	return temp;	// return empty TCB node if not found in queue
}

// gets index of non-empty queue with highest priority
uint8_t highestPriorityQ() {
	uint8_t leadingZeros = 0;
	// intrinsic assembly command to get leading zeros in bit vector
	leadingZeros = __clz(bitVector);	
	return (31 - leadingZeros);
}

TCB_t tcbList[6]; 	// declare six task control blocks
TCB_t * runningTask;	// keep track of running task
typedef void (*rtosTaskFunc_t)(void *args);

typedef struct {	// implement semaphores
	int32_t s;
	queue_t waitList;
}  sem_t;

void initSem(sem_t * sem, int32_t count){	// initialize semaphore
	// initialize semaphore count
	sem->s = count;		
	// initialize wait list
	sem->waitList.head = NULL;
	sem->waitList.size = 0;
}

void wait(sem_t * sem){		// to acquire semaphore
	__disable_irq();	// disable interrupts
	if (sem->s <= 0) { // if no more tasks can access semaphore
		//current task gets added to semaphore's waitlist
		enqueue(&sem->waitList, runningTask); 
		// current task's status goes from running to waiting
		runningTask->state = waiting;
		printf("wait\n");
	}
	sem->s--;	// decrement semaphore count
	__enable_irq();		// enable interrupts
}

void signal(sem_t * sem){	// to release semaphore
	__disable_irq();
	if (sem->s <= 0) { // if there are still tasks in the waitlist
		if (sem->waitList.size) {
			// dequeue one task from waitlist
			TCB_t * toRun = dequeue(&(sem->waitList)); 
			// enqueue that task back to ready queue
			enqueue(&(priorityQ[toRun->priority]), toRun); 
			toRun->state = ready;		// update state
		}
		printf("signal\n");
	}
	sem->s++;	// increment semaphore count
	__enable_irq();
}

typedef struct {	// implement mutexes
	int32_t s;
	queue_t waitList;
	uint8_t owner;
	uint8_t oldPriority;
}  mutex_t;

void initMutex(mutex_t * mutex){	// initialize mutex
	// initialize count and wait list
	mutex->s = 0x01;
	mutex->waitList.head = NULL;
	mutex->waitList.size = 0;
}

void lock(mutex_t * mutex){		// to acquire mutex
	__disable_irq();
	if (mutex->s){		// if mutex is available
		mutex->s--; // decrement count - lock mutex
		mutex->owner = runningTask->taskID; // set owner of mutex
		// save owner's priority for priority inheritance
		mutex->oldPriority = tcbList[mutex->owner].priority;
		printf("locked by task %d\n", mutex->owner);
	}
	else { // mutex is 0, or locked by another task
		// check if task requesting mutex has higher priority than owner
		if (runningTask->priority > tcbList[mutex->owner].priority){
			// raise priority of owner to that of waiting task
			TCB_t * updatedPriorityTask = removeTCB(&(tcbList[mutex->owner]));
			enqueue(&(priorityQ[runningTask->priority]), updatedPriorityTask);
			tcbList[mutex->owner].priority = runningTask->priority; 
		}
		// check that requesting task is not the owner
		if (runningTask->taskID != mutex->owner){
			// add running task to mutex's waitlist and update task's state
			enqueue(&(mutex->waitList), runningTask); 
			runningTask->state = waiting;
			printf("task %d blocked, waiting for task %d to release\n", runningTask->taskID, mutex->owner);
		}
	}
	__enable_irq();
}

void release(mutex_t * mutex){	// to release mutex
	__disable_irq();
	if (mutex->owner == runningTask->taskID){ // owner test on release
		// check if owner's priority was temporarily raised
		if (runningTask->priority != mutex->oldPriority) {
			// restore original priority
			runningTask->priority = mutex->oldPriority;
		}
		mutex->s++;	// increment count - release mutex
		
		// remove first task from wait list and add to ready queue
		if (mutex->waitList.size){
			TCB_t * toRun = dequeue(&mutex->waitList);
			enqueue(&(priorityQ[toRun->priority]), toRun);
			toRun->state = ready; // update task's state
			
			// assign mutex to task dequeued from wait list
			mutex->owner = toRun->taskID;
			mutex->s--;
			mutex->oldPriority = toRun->priority;
			
			printf("enqueued task %d and assigned mutex to task %d; ", toRun->taskID, mutex->owner);
		}
		printf("released by task %d\n", runningTask->taskID);
	}
	__enable_irq();
}

void switchContext(uint8_t a, uint8_t b) {	// performs context switch
	// store registers for running task to stack
	tcbList[a].taskSP = storeContext();	  

	// change states
	if (tcbList[a].state == running)
		tcbList[a].state = ready;
	tcbList[b].state = running;

	// push new task's stack contents to registers
	restoreContext(tcbList[b].taskSP);
}

void PendSV_Handler(){	// invoked for context switches
	// get index of highest priority non-empty queue
	uint8_t nextQ = highestPriorityQ();	

	// if next queue is lower priority than running task - no context switch
	if (nextQ >= runningTask->priority) {
		// get next ready task in queue
		TCB_t * taskToRun = dequeue(&priorityQ[nextQ]);

		// add running task back to queue (unless waiting for semaphore)
		if (runningTask->state != waiting)
			enqueue(&priorityQ[runningTask->priority], runningTask);

		// switch context between running task and next task
		uint8_t prevRunning = runningTask->taskID;
		runningTask = taskToRun;
		switchContext(prevRunning, runningTask->taskID);
	}
}

void init(void){	// initialize stack
	// initialize TCBs
	uint32_t * vectorTable = 0x0;
	uint32_t mainStackBase = vectorTable[0];
	
	for (uint8_t i = 0; i < 6; i++){
		tcbList[i].taskBase = (mainStackBase - 2048) - (1024*(5-i));
		tcbList[i].taskSP = tcbList[i].taskBase;
		tcbList[i].taskID = i;
		tcbList[i].state = inactive;
	}
	
	// 2) copy Main stack contents to process stack
	uint32_t mainSP = __get_MSP();
	for (uint32_t i = 0; i < 1024; i++){
		*((uint32_t *)(tcbList[0].taskBase - i)) = *((uint32_t *)(mainStackBase - i));
	}
	tcbList[0].taskSP = tcbList[0].taskBase - (mainStackBase - mainSP);
	tcbList[0].state = running;
	tcbList[0].priority = idleDemon;

	// setting running task node to task main
	runningTask = &tcbList[0];
	runningTask->next = NULL;

	__set_MSP(mainStackBase);
	__set_CONTROL(__get_CONTROL() | SPBIT);
	__set_PSP(tcbList[0].taskSP);
}

uint8_t createTask(rtosTaskFunc_t funcPtr, void * args, uint8_t p) {
	int i = 1;

	// find next empty stack
	while(tcbList[i].taskSP != tcbList[i].taskBase) {
		i++;
		if (i >= 6)
			return 0;
	}
	
	// set it to ready to run
	tcbList[i].state = ready;
	
	// set task's priority 
	tcbList[i].priority = p;
	
	// PSR
	tcbList[i].taskSP -= 4;
	*((uint32_t *)tcbList[i].taskSP) = (uint32_t)0x01000000;
	
	// PC
	tcbList[i].taskSP -= 4;
	*((uint32_t *)tcbList[i].taskSP) = (uint32_t)funcPtr;

	// LR to R1
	for (uint8_t x = 0; x < 5; x++) {
		tcbList[i].taskSP -= 4;
		*((uint32_t *)tcbList[i].taskSP) = (uint32_t)0x00;
	}

	// R0
	tcbList[i].taskSP -= 4;
	*((uint32_t *)tcbList[i].taskSP) = *(uint32_t *)args;

	// R11 to R4
	for (uint8_t x = 0; x < 8; x++) {
		tcbList[i].taskSP -= 4;
		*((uint32_t *)tcbList[i].taskSP) = (uint32_t)0x00;
	}
	
	// create a new node for the task
	TCB_t * newNode = &tcbList[i];
	enqueue(&priorityQ[tcbList[i].priority], newNode);
	//bitVector |= 1 << tcbList[i].priority;
	
	return 1;
}


mutex_t mtx;
sem_t sem;

void task_4(void* s) {	// blink all LEDs
	while(1) {
		lock(&mtx);
		__disable_irq();
		printf("task_4_started\n");
		__enable_irq();
		LPC_GPIO2->FIOCLR |= 0x0000007C;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOSET |= 0x0000007C;
		release(&mtx);
		__disable_irq();
		printf("task_4_done\n");
		__enable_irq();
		for(int i=0; i<12000000; i++);
	}
}

void task_1(void* s){	// blink LED 6
	//lock(&mtx);
	while(1) {
		//wait(&sem);
		lock(&mtx);
		__disable_irq();
		printf("task_1_started\n");
		__enable_irq();
		LPC_GPIO2->FIOSET = 1 << 6;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOCLR = 1 << 6;
		//signal(&sem);
		release(&mtx);
		__disable_irq();
		printf("task_1_done\n");
		__enable_irq();
		for(int i=0; i<12000000; i++);
	}
}

void task_2(void* s){	// blink LED 5
	while(1) {
		//wait(&sem);
		lock(&mtx);
		__disable_irq();
		printf("task_2_started\n");
		__enable_irq();
		LPC_GPIO2->FIOSET = 1 << 5;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOCLR = 1 << 5;
		//signal(&sem);
		release(&mtx);
		__disable_irq();
		printf("task_2_done\n");
		__enable_irq();
		for(int i=0; i<12000000; i++);
	}
}

/* for mutex testing
void task_3(void* s){	// blink LED 4
	while(1) {
		lock(&mtx);
		__disable_irq();
		printf("task_2_started\n");
		__enable_irq();
		LPC_GPIO2->FIOSET = 1 << 4;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOCLR = 1 << 4;
		release(&mtx);
		__disable_irq();
		printf("task_2_done\n");
		__enable_irq();
		for(int i=0; i<12000000; i++);
	}
}*/

uint8_t count = 0;
void task_3(void* s){	// blink LED 4
	while(1) {
		//wait(&sem);
		count++;
		lock(&mtx);
		__disable_irq();
		printf("task_3_started\n");
		__enable_irq();
		LPC_GPIO2->FIOSET = 1 << 4;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOCLR = 1 << 4;
		//signal(&sem);
		if (count < 3)
			release(&mtx);
		__disable_irq();
		printf("task_3_done\n");
		__enable_irq();
		for(int i=0; i<12000000; i++);
		
		if (count == 3) {
			for(int i=0; i<12000; i++);
			rtosTaskFunc_t p4 = task_4;
			createTask(p4, s, superDuperImportant);
			for(int i=0; i<12000; i++);
		}
		if (count >= 3) {
			__disable_irq();
			printf("task 3 running uninterrupted\n");
			__enable_irq();
			for (uint8_t itr = 0; itr < 3; itr++){
				LPC_GPIO2->FIOSET = 1 << 3;
				for(int i=0; i<12000000; i++);
				LPC_GPIO2->FIOCLR = 1 << 3;
				for(int i=0; i<12000000; i++);
			}
			release(&mtx);
		}
	}
}

int main(void) {
	//initialize all LEDs
	LPC_GPIO2->FIODIR |= 0x0000007C;
	LPC_GPIO1->FIODIR |= ((uint32_t)11<<28);

	//turn off all LEDs
	LPC_GPIO2->FIOCLR |= 0x0000007C;
	LPC_GPIO1->FIOCLR |= ((uint32_t)11<<28);

	// initialize systick, stack, queues, mutex
	SysTick_Config(SystemCoreClock/1000);
	init();		
	initializeQ();
	initMutex(&mtx);
	//initSem(&sem, 3);
	
	// create new tasks
	rtosTaskFunc_t p1 = task_1;
	char *s = "test_param";
	createTask(p1, s, normal);
	
	rtosTaskFunc_t p2 = task_2;
	createTask(p2, s, normal);
	
	rtosTaskFunc_t p3 = task_3;
	createTask(p3, s, normal);
	
	while(1) {		// blink LED 2
		LPC_GPIO2->FIOSET = 1 << 2;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOCLR = 1 << 2;
		for(int i=0; i<12000000; i++);
	}
}
