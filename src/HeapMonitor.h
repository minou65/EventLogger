// HeapMonitor.h

#ifndef _HEAPMONITOR_h
#define _HEAPMONITOR_h

#if defined(ARDUINO) && ARDUINO >= 100
	#include "arduino.h"
#else
	#include "WProgram.h"
#endif

void loopHeapMonitor();
void beginHeapMonitor();

#endif

