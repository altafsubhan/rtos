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

void SysTick_Handler(void) {
	msTicks++;
	
	SCB->ICSR |= (0x01 << 28);
}

enum priorities{
		idleDemon,
		low,
		belowNormal,
		normal,
		aTeeeeensyBitAmountAboveNormal,
		superDuperImportant
} priority;

enum states{
		inactive,
		waiting,
		ready,
		running
} state;

typedef struct {
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

void removeTCB(TCB_t * taskToRun) {
	queue_t * list = &priorityQ[taskToRun->priority];
	
	// if head element
	if (list->head->task == taskToRun){
		list->head = list->head->next;
		list->size--;
		return;
	}
	
	// h -> ** -> ** -> ** -> ** -> ** -> 0

	// If middle  or last element
	node_t * iter = list->head;
	while (iter->next != NULL) {
		if (iter->next->task == taskToRun) {
				iter->next = iter->next->next;
				list->size--;
				return;
		}
		iter = iter->next;
	}
}

node_t * dequeue(queue_t * list) {
	node_t * currNode = list->head;
	list->head = list->head->next;
	list->size--;
	return currNode;
}

TCB_t * getTaskToRun() {
	uint8_t i = 5;
	while (i >= 0) {
			if (priorityQ[i].size > 0){
			node_t * iter = priorityQ[i].head;
			while (iter->task->state != ready && iter->next != NULL) {
				iter = iter->next;
			}
			if (iter->task->state == ready)
				return iter->task;
			i--;
		}
	}
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
	if (!(sem->s >= 0)) {
		enqueue(sem->waitList, runningTask);
		runningTask->task->state = waiting;
	}
	__enable_irq();
}

void signal(sem_t * sem){
	__disable_irq();
	sem->s++;
	if (sem->s <= 0) {
		node_t * toRun = dequeue(sem->waitList);
		toRun->task->state = ready;
	}
	__enable_irq();
}

// priority array of task queues
// each queue has tasks 
// determine highest priority
// implement round-robin in that queue until depleted
// 

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
	TCB_t * nextTask = getTaskToRun();
	removeTCB(nextTask);
	enqueue(&priorityQ[runningTask->task->priority], runningTask);
	uint8_t prevRunning = runningTask->task->taskID;
	runningTask->task = nextTask;
	
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
	node_t * newNode;
	newNode->task = &tcbList[i];
	enqueue(&priorityQ[tcbList[i].priority], newNode);
	
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
