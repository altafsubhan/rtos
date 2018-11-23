/*
 * Default main.c for rtos lab.
 * @author Subhan and Susan, 2018
 */

#include <LPC17xx.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "context.h"

#define SPBIT 0x02
uint32_t msTicks = 0;
uint8_t bitVector = 0;

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

typedef struct { // task control block
	uint8_t taskID;
	uint32_t taskBase;
	uint32_t taskSP;
	
	uint8_t priority;
	uint8_t state;
	
} TCB_t;

typedef struct node { 
	TCB_t * task;
	struct node* next;
} node_t;

typedef struct queue {
	node_t * head;
	uint8_t size;
} queue_t;

void enqueue(queue_t * list, node_t * toAdd) {
	node_t * listNode = list->head;
	while (listNode->next != NULL) {
		listNode = listNode->next;
	}
	listNode->next = toAdd;
	toAdd->next = NULL;
	list->size++;
}	

queue_t priorityQ[7];

void initializeQ() {
		for (int i = 0; i < 7; i++){
			priorityQ[i].head = NULL;
			priorityQ[i].size = 0;
		}
}

node_t * dequeue(queue_t * list) {
	node_t * currNode = list->head;
	list->head = list->head->next;
	list->size--;
	return currNode;
}

node_t * removeTCB(TCB_t * taskToDel) {
	queue_t * list = &priorityQ[taskToDel->priority];
	node_t * temp;
	
	// if head element
	if (list->head->task == taskToDel){
		temp = list->head;
		list->head = list->head->next;
		list->size--;
		return temp;
	}
	
	// h -> ** -> ** -> ** -> ** -> ** -> 0

	// If middle  or last element
	node_t * iter = list->head;
	while (iter->next != NULL) {
		if (iter->next->task == taskToDel) {
				temp = iter->next;
				iter->next = iter->next->next;
				list->size--;
				return temp;
		}
		iter = iter->next;
	}
}

uint8_t highestPriorityQ() {
	//assembly commands intrinsics?????? O(1) time!!!!!!!!!!
	uint8_t leadingZeros = 0;
	__ASM ("CLZ %[result], %[bitV]"
	: [result] "=r" (leadingZeros)
	: [bitV] "r" (bitVector));
	return (7 - leadingZeros);
}

TCB_t tcbList[6]; 
node_t * runningTask;
typedef void (*rtosTaskFunc_t)(void *args);


typedef struct {
	int32_t s;
	queue_t * waitList;
}  sem_t;

void initSem(sem_t * sem, int32_t count){
	sem->s = count;
	sem->waitList->head = NULL;
	sem->waitList->size = 0;
}

void wait(sem_t * sem){
	__disable_irq();
	sem->s--;
	if (sem->s <= 0) { // if no more tasks can access semaphore
		enqueue(sem->waitList, runningTask); //current task gets added to semaphore's waitlist
		runningTask->task->state = waiting; // current task's status goes from running to waiting
	}
	// does this task now need to stop running? how to make it stop?
	__enable_irq();
}

void signal(sem_t * sem){
	__disable_irq();
	sem->s++;
	if (sem->s <= 0) { // if there are still tasks in the waitlist
		node_t * toRun = dequeue(sem->waitList); // dequeue one task from waitlist
		enqueue(&priorityQ[toRun->task->priority], toRun); // enqueue that task back to ready queues
		toRun->task->state = ready;
	}
	__enable_irq();
}

typedef struct {
	int32_t s;
	queue_t * waitList;
	uint8_t owner;
	uint8_t oldPriority;
}  mutex_t;

void initMutex(mutex_t * mutex, int32_t count){
	mutex->s = count;
	mutex->waitList->head = NULL;
	mutex->waitList->size = 0;
}

void lock(mutex_t * mutex){
	__disable_irq();
	if (mutex->s){
		mutex->s--; // task locks mutex
		mutex->owner = runningTask->task->taskID; // set owner of mutex
		mutex->oldPriority = tcbList[mutex->owner].priority;
	}
	else if (!(mutex->s)){ // mutex is 0, or locked by another task
		if (runningTask->task->priority > tcbList[mutex->owner].priority){
			node_t * updatedPriorityTask = removeTCB(&tcbList[mutex->owner]);
			enqueue(&priorityQ[runningTask->task->priority], updatedPriorityTask);
			tcbList[mutex->owner].priority = runningTask->task->priority; // set priority of task with mutex to be equal as waiting task
		}
		enqueue(mutex->waitList, runningTask); // add the running task to mutex's waitlist
		runningTask->task->state = waiting; // set task's status to waiting
	}
	__enable_irq();
}

void release(mutex_t * mutex, uint8_t taskID){
	__disable_irq();
	if (mutex->owner == taskID){ // owner test on release
		if (runningTask->task->priority != mutex->oldPriority) {
			runningTask->task->priority = mutex->oldPriority;
		}
		mutex->s++;	// allow owner of mutex to release
		node_t * toRun = dequeue(mutex->waitList); // remove task from mutex's waitlist
		enqueue(&priorityQ[toRun->task->priority], toRun); // enqueue task back to ready queue
		toRun->task->state = ready; // set task's status back to ready
	}
	__enable_irq();
}

void switchContext(uint8_t a, uint8_t b) {
	// store registers to stack
	tcbList[a].taskSP = storeContext();	  

	// change states
	if (tcbList[a].state == running)
		tcbList[a].state = ready;
	tcbList[b].state = running;

	// push new task's stack contents to registers
	restoreContext(tcbList[b].taskSP);
}

void PendSV_Handler(){
	uint8_t nextQ = highestPriorityQ();
	node_t * taskToRun = dequeue(&priorityQ[nextQ]);
	enqueue(&priorityQ[runningTask->task->priority], runningTask);
	uint8_t prevRunning = runningTask->task->taskID;
	runningTask->task = taskToRun->task;
	switchContext(prevRunning, runningTask->task->taskID);
}


void init(void){	
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

	// setting running task node to task main
	runningTask->task = &tcbList[0];
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
	node_t * newNode;
	newNode->task = &tcbList[i];
	enqueue(&priorityQ[tcbList[i].priority], newNode);
	bitVector |= 1 << tcbList[i].priority;
	
	return 1;
}

void task_1(void* s){
	// blink LED 6
	while(1) {
		LPC_GPIO2->FIOSET = 1 << 6;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOCLR = 1 << 6;
		for(int i=0; i<12000000; i++);
	}
}

int main(void) {
	//initialize all LEDs
	LPC_GPIO2->FIODIR |= 0x0000007C;
	LPC_GPIO1->FIODIR |= ((uint32_t)11<<28);
		
	//turn off all LEDs
	LPC_GPIO2->FIOCLR |= 0x0000007C;
	LPC_GPIO1->FIOCLR |= ((uint32_t)11<<28);

	// initialize systick
	SysTick_Config(SystemCoreClock/1000);
	init();		// initialize stack for each task
	
	// create new task
	rtosTaskFunc_t p = task_1;
	char *s = "test_param";
	createTask(p, s, idleDemon);
	
	// blink LED 4
	while(1) {
		LPC_GPIO2->FIOSET = 1 << 4;
		for(int i=0; i<12000000; i++);
		LPC_GPIO2->FIOCLR = 1 << 4;
		for(int i=0; i<12000000; i++);
	}
}
