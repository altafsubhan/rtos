/*
 * Default main.c for rtos lab.
 * @author Andrew Morton, 2018
 */
#include <LPC17xx.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define SPBIT 0x02
uint32_t msTicks = 0;

void SysTick_Handler(void) {
    msTicks++;
}

typedef struct {
	uint8_t taskID;
	uint32_t taskBase;
	uint32_t taskSP;
} TCB_t;

TCB_t tcbList[6]; 

void init(void){	
	// initialize TCBs
	uint32_t * vectorTable = 0x0;
	uint32_t mainStackBase = vectorTable[0];
	
	for (uint8_t i = 0; i < 6; i++){
		tcbList[i].taskBase = (mainStackBase - 2048) - (1024*(5-i));
		tcbList[i].taskSP = tcbList[i].taskBase;
	}
	
	// 2) copy Main stack contents to process stack
	uint32_t mainSP = __get_MSP();
	for (uint32_t i = 0; i < 1024; i++){
		*((uint32_t *)(tcbList[0].taskBase - i)) = *((uint32_t *)(mainStackBase - i));
	}
	tcbList[0].taskSP = tcbList[0].taskBase - (mainStackBase - mainSP);

	__set_MSP(mainStackBase);
	__set_CONTROL(__get_CONTROL() | SPBIT);
	__set_PSP(tcbList[0].taskSP);

	printf("hello\n");
}

/*bool createTask(rtosTaskFunc_t funcPtr, void * args) {
	int i = 1;
	while(tcbList[i].taskStackPtr != tchList[i].taskBaseAddress) {
		i++;
		if (i >= 6)
			return 0;
	}
	

	// PSR
	*tcbList[i].taskStackPtr = (uint32_t)0x01000000;
	taskStackPtr--;
	
	// PC
	*tcbList[i].taskStackPtr = funcPtr;
	taskStackPtr--;

	// LR to R1
	for (x = 0; x < 5; x++) {
		*tcbList[i].taskStackPtr = (uint32_t)0x00;
		taskStackPtr--;
	}

	// R0
	*tcbList[i].taskStackPtr = args;

	// R11 to R4
	for (x = 0; x < 8; x++) {
		*tcbList[i].taskStackPtr = (uint32_t)0x00;
		taskStackPtr--;
	}
	taskStackPtr++;
}*/

int main(void) {
	/*SysTick_Config(SystemCoreClock/1000);
	printf("\nStarting...\n\n");
	
	uint32_t period = 1000; // 1s
	uint32_t next = -period;
	while(true) {
		if(msTicks - next >= period) {
			printf("tick ");
			next += period;
		}
	}*/
	/*
	uint32_t test = 5;
	uint32_t test2 = 6;
	printf("%p, %p\n", &test, &test2);*/
	
	init();
	
	//printf("%d, %p, %d, %p\n", test, &test, test2, &test2);
	
}
