#pragma once
// Minimal Arduino stand-in so oil_sensor.cpp can run on a PC against simulated chips.
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
extern uint32_t g_now;
static inline uint32_t millis() { return g_now; }
struct SerialT { void printf(const char* f, ...) __attribute__((format(printf,2,3))) ;
                 void println(const char* s) { puts(s); } void println() { puts(""); } } ;
extern SerialT Serial;
#include <stdarg.h>
inline void SerialT::printf(const char* f, ...) { va_list a; va_start(a, f); vprintf(f, a); va_end(a); }
