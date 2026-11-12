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

// Prints a detailed heap report (uptime, free heap, min free heap,
// largest free block, fragmentation, change since boot) via webPrintln.
void printHeapReport();

// Configures the warning thresholds used by checkHeapWarnings().
// freeHeapBytes_: emit a warning when the free heap stays below this value.
// fragmentationPercent_: emit a warning when fragmentation stays above this value (0-100).
// A threshold must be exceeded continuously for HEAPMONITOR_WARNING_SUSTAIN_MS
// (default 30 s) before a warning is raised.
void setHeapWarningThresholds(uint32_t freeHeapBytes_, uint8_t fragmentationPercent_);

#endif

