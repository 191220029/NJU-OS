#include "common.h"
#include "x86.h"
#include "device.h"

void kEntry(void) {

	// Interruption is disabled in bootloader

	initSerial();// initialize serial port
	// TODO: 做一系列初始化
	        	// initialize idt
				// iniialize 8259a
		 		// initialize gdt, tss
	initVga(); // initialize vga device
				// initialize keyboard device
	loadUMain(); // load user program, enter user space

	while(1);
	assert(0);
}
