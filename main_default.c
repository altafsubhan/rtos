/*
 * Default main.c for rtos lab.
 * @author Andrew Morton, 2018
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

typedef struct {
	uint8_t taskID;
	uint32_t taskBase;
	uint32_t taskSP;
	
	enum states{
		inactive,
		waiting,
		ready,
		running
	} state;
	
} TCB_t;

TCB_t tcbList[6]; 
typedef void (*rtosTaskFunc_t)(void *args);

void PendSV_Handler(){
	uint8_t i = 0;
	while (tcbList[i].state != running) {
		i++;
	}
	
	uint8_t j = i;
	while (tcbList[j].state != ready) {
		j = (j+1)%6;
	}
	
	tcbList[i].taskSP = storeContext();
	tcbList[i].state = ready;
	restoreContext(tcbList[1].taskSP);
	tcbList[1].state = running;
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

	__set_MSP(mainStackBase);
	__set_CONTROL(__get_CONTROL() | SPBIT);
	__set_PSP(tcbList[0].taskSP);
}

uint8_t createTask(rtosTaskFunc_t funcPtr, void * args) {
	int i = 1;
	while(tcbList[i].taskSP != tcbList[i].taskBase) {
		i++;
		if (i >= 6)
			return 0;
	}
	
	// set it to ready to run
	tcbList[i].state = ready;
	
	// PSR
	tcbList[i].taskSP -= 3;
	*((uint32_t *)tcbList[i].taskSP) = (uint32_t)0x01000000;
	
	// PC
	tcbList[i].taskSP -= 4;
	*((uint32_t *)tcbList[i].taskSP) = *(uint32_t *)funcPtr;

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
	//tcbList[i].taskSP += 4;
	
	return 1;
}

void delay(uint8_t time){
	for (uint8_t y = 0; y < time; y++){
		for (uint32_t i = 0; i < (100); i++) {
		}
	}
}

void sayHi(void* s){
	//printf("Hello %s", s);
	while (true) {
		//LPC_GPIO2->FIOCLR = 1 << 6;
		//delay(5);
		LPC_GPIO2->FIOSET = 1 << 6;
	}
}


int main(void) {
	//initialize all LEDs
	LPC_GPIO2->FIODIR |= 0x0000007C;
	LPC_GPIO1->FIODIR |= ((uint32_t)11<<28);
		
	//turn off all LEDs
	LPC_GPIO2->FIOCLR |= 0x0000007C;
	LPC_GPIO1->FIOCLR |= ((uint32_t)11<<28);

	SysTick_Config(SystemCoreClock/1000);
	init();
	
	rtosTaskFunc_t p = &sayHi;
	char *s = "Susan";
	createTask(p, s);
	
	uint32_t period = 1000; // 1s
	uint32_t next = -period;
	while(true) {
		if(msTicks - next >= period) {
			LPC_GPIO2->FIOSET = 1 << 4;
			next += period;
		} else {
			LPC_GPIO2->FIOCLR = 1 << 4;
		}
	}
	
	/*
	uint32_t test = 5;
	uint32_t test2 = 6;
	printf("%p, %p\n", &test, &test2);*/
	//printf("%d, %p, %d, %p\n", test, &test, test2, &test2);
	
}
