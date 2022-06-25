#include "x86.h"
#include "device.h"


extern int displayRow;
extern int displayCol;

extern TSS tss;
extern int current;
extern ProcessTable pcb[MAX_PCB_NUM];

void GProtectFaultHandle(struct StackFrame *sf);

void syscallHandle(struct StackFrame *sf);

void timerHandle(struct StackFrame *sf);
void syscallFork(struct StackFrame *sf);
void syscallSleep(struct StackFrame *sf);
void syscallExit(struct StackFrame *sf);

void syscallWrite(struct StackFrame *sf);
void syscallPrint(struct StackFrame *sf);


extern int signal;

extern void P(int signal);
extern void V(int signal);

void irqHandle(struct StackFrame *sf) { // pointer sf = esp
	/* Reassign segment register */
	asm volatile("movw %%ax, %%ds"::"a"(KSEL(SEG_KDATA)));
	/*TODO Save esp to stackTop */
	uint32_t stackTop = pcb[current].stackTop;
	pcb[current].prevStackTop = pcb[current].stackTop;
	pcb[current].stackTop = (uint32_t)sf;

	switch(sf->irq) {
		case -1:
			break;
		case 0xd:
			GProtectFaultHandle(sf);
			break;
		case 0x20:
			timerHandle(sf);
			break;
		case 0x80:
			syscallHandle(sf);
			break;
		default:assert(0);
	}
	/*TODO Recover stackTop */
	//pcb[current].stackTop = pcb[current].prevStackTop;
	pcb[current].stackTop = stackTop;
}

void GProtectFaultHandle(struct StackFrame *sf) {
	assert(0);
	return;
}


void syscallHandle(struct StackFrame *sf) {
	switch(sf->eax) { // syscall number
		case 0:
			syscallWrite(sf);
			break; // for SYS_WRITE
		/*TODO Add Fork,Sleep... */
		case 1:
			syscallFork(sf);
		case 2: break; //SYS_EXEC not implemented 
		case 3: syscallSleep(sf); break;
		case 4: syscallExit(sf); break;
			
		default:break;
	}
}

void syscallWrite(struct StackFrame *sf) {
	switch(sf->ecx) { // file descriptor
		case 0:
			syscallPrint(sf);
			break; // for STD_OUT
		default:break;
	}
}

void syscallPrint(struct StackFrame *sf) {
	
	int sel = sf->ds; //TODO segment selector for user data, need further modification
	char *str = (char*)sf->edx;
	int size = sf->ebx;
	int i = 0;
	int pos = 0;
	char character = 0;
	uint16_t data = 0;
	//int s = 0;
	//asm volatile("movw (%0), %%es"::"a"(KSEL(SEG_KDATA)));
	//asm volatile("movl %%es:(%0), %1"::"r"(signal), "r"(s));
	P(signal);
	asm volatile("movw %0, %%es"::"m"(sel));
	//disableInterrupt();
	for (i = 0; i < size; i++) {
		asm volatile("movb %%es:(%1), %0":"=r"(character):"r"(str+i));
		if(character == '\n') {
			displayRow++;
			displayCol=0;
			if(displayRow==25){
				displayRow=24;
				displayCol=0;
				scrollScreen();
			}
		}
		else {
			data = character | (0x0c << 8);
			pos = (80*displayRow+displayCol)*2;
			asm volatile("movw %0, (%1)"::"r"(data),"r"(pos+0xb8000));
			displayCol++;
			if(displayCol==80){
				displayRow++;
				displayCol=0;
				if(displayRow==25){
					displayRow=24;
					displayCol=0;
					scrollScreen();
				}
			}
		}
		//asm volatile("int $0x20"); //XXX Testing irqTimer during syscall
		//asm volatile("int $0x20":::"memory"); //XXX Testing irqTimer during syscall
	}
	updateCursor(displayRow, displayCol);
	V(signal);
	//TODO take care of return value
	return;
}

void timerHandle(struct StackFrame *sf) {
	int i;
	uint32_t stackTop;

	//putChar(pcb[current].pid + 0x30);
	//decrease sleepTime of blocked process, if sleepTime is 0, make it runnable
	for (i = 0; i < MAX_PCB_NUM; i++)
		if (pcb[i].state == STATE_BLOCKED) {
			pcb[i].sleepTime --;
			if(pcb[i].sleepTime == 0)
				pcb[i].state = STATE_RUNNABLE;
		}

	//increase timeCount of current process
	pcb[current].timeCount++;
	
	//if current process timeCount up to max and other process is runnable, switch it
	if (pcb[current].timeCount >= MAX_TIME_COUNT || pcb[current].state != STATE_RUNNING) {
		if(pcb[current].state == STATE_RUNNING)		
			pcb[current].state = STATE_RUNNABLE;
		pcb[current].timeCount = 0; //reset current process's timeCount
		for (i = (current + 1) % MAX_PCB_NUM; i != current; i = (i + 1) % MAX_PCB_NUM)
			if (pcb[i].state == STATE_RUNNABLE) //find one runnable process ecxept current one
				break;
		//if(i != current) {
		current = i;
		pcb[current].state = STATE_RUNNING; //select next runnable process 
		//}
		//else {
			//enableInterrupt();
			//while(1)
			//	waitForInterrupt();
		//}
	}

	//putChar('T'); putChar(pcb[current].pid + 0x30);putChar('\n');
	//recover stackTop
	stackTop = pcb[current].stackTop;
	pcb[current].stackTop = pcb[current].prevStackTop;

	tss.esp0 =  (uint32_t)&(pcb[current].stackTop);
	asm volatile("movl %0, %%esp" : :"m"(stackTop));
	asm volatile("popl %gs");
	asm volatile("popl %fs");
	asm volatile("popl %es");
	asm volatile("popl %ds");
	asm volatile("popal");
	asm volatile("addl $8, %esp");
	asm volatile("iret");
}

void syscallFork(struct StackFrame *sf) {
	//find one free(dead) pcb
	int i;
	for (i = 0; i < MAX_PCB_NUM; i++) {
		if (pcb[i].state == STATE_DEAD)
			break;
	}

	if (i != MAX_PCB_NUM) {//free pcb found
		//test interrupt nesting

		enableInterrupt();
		for (int j = 0; j < 0x100000; j++) { 
			//copy memory
			*(uint8_t *)(j + (i + 1) * 0x100000) = *(uint8_t *)(j + (current + 1) * 0x100000);
			//int tmp = j;
			//putChar('F');putChar('o');putChar('r');putChar('k');putChar(':');
			//for(int k = 31; k >= 0; k--) {
			//	putChar(((((tmp & (1 << k)) >> k) & 0x1) + 0x30));
			//	if(k != 32 && !(k % 4)) putChar(' ');
			//}			
			//putChar('\n');
			//if(!(j % 0x1000))
			//asm volatile("int $0x20"); //Testing irqTimer during syscall
		}
		disableInterrupt();

		//copy resource of father process(current process)
		//pcb[i] = pcb[current];
		//*(ProcessTable *)(&pcb[i]) = *(ProcessTable *)(&pcb[current]);
		for (int j = 0; j < sizeof(ProcessTable); j++)
			*((uint8_t *)(&pcb[i]) + j) = *((uint8_t *)(&pcb[current]) + j);

		//user process
		pcb[i].stackTop = (uint32_t)&(pcb[i].regs);
		pcb[i].prevStackTop = (uint32_t)&(pcb[i].stackTop);
		pcb[i].state = STATE_RUNNABLE;
		pcb[i].timeCount = 0;
		pcb[i].sleepTime = 0;
		pcb[i].pid = i;
	
		pcb[i].regs.gs = USEL(2 + 2 * i);
		pcb[i].regs.fs = USEL(2 + 2 * i);
		pcb[i].regs.es = USEL(2 + 2 * i);
		pcb[i].regs.ds = USEL(2 + 2 * i);
		pcb[i].regs.cs = USEL(1 + 2 * i);
		pcb[i].regs.ss = USEL(2 + 2 * i);
		
		//return value
		pcb[i].regs.eax = 0;
		pcb[current].regs.eax = i;
		putChar('F'); putChar(0x30 + pcb[i].pid);putChar('\n');
	}
	else { //fork failed
		putChar('F');putChar('-');putChar('1');putChar('\n');
		pcb[current].regs.eax = -1;
	}
	return;
}


void syscallSleep(struct StackFrame *sf) {

	//block current process and set sleepTime
	//disableInterrupt();
	pcb[current].state = STATE_BLOCKED;
	pcb[current].sleepTime = sf->ecx;
	putChar('s'); putChar(0x30 + pcb[current].pid); putChar('\n');

	//switch process by timerHandle
	//enableInterrupt();
	asm volatile("int $0x20");
	return;

}

void syscallExit(struct StackFrame *sf) {
	//disableInterrupt();

	//kill current process
	pcb[current].state = STATE_DEAD;
	putChar('E'); putChar(0x30 + pcb[current].pid);putChar('\n');

	//enableInterrupt();
	asm volatile("int $0x20");
	return;
}


