#ifndef PORTMACRO_H
#define PORTMACRO_H

/* Host-side syntax/static-analysis substitute for the ARMCC FreeRTOS port.
 * This file is never part of the target build. It deliberately models only
 * types and scheduling/critical-section macros needed to parse first-party C. */

#include <stdint.h>

#define portCHAR               char
#define portFLOAT              float
#define portDOUBLE             double
#define portLONG               long
#define portSHORT              short
#define portSTACK_TYPE         uint32_t
#define portBASE_TYPE          long

typedef uint32_t StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;
typedef uint32_t TickType_t;

#define portMAX_DELAY          ((TickType_t)0xFFFFFFFFUL)
#define portSTACK_GROWTH       (-1)
#define portTICK_PERIOD_MS     ((TickType_t)1000U / configTICK_RATE_HZ)
#define portBYTE_ALIGNMENT     8
#define portTICK_TYPE_IS_ATOMIC 1

#define portYIELD()                         ((void)0)
#define portYIELD_FROM_ISR(value)           ((void)(value))
#define portEND_SWITCHING_ISR(value)        ((void)(value))
#define portDISABLE_INTERRUPTS()            ((void)0)
#define portENABLE_INTERRUPTS()             ((void)0)
#define portENTER_CRITICAL()                ((void)0)
#define portEXIT_CRITICAL()                 ((void)0)
#define portSET_INTERRUPT_MASK_FROM_ISR()   (0UL)
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(value) ((void)(value))
#define portASSERT_IF_INTERRUPT_PRIORITY_INVALID() ((void)0)
#define portTASK_FUNCTION_PROTO(function, parameter) void function(void *parameter)
#define portTASK_FUNCTION(function, parameter)       void function(void *parameter)
#define portNOP()                            ((void)0)
#define portINLINE                          inline
#define portFORCE_INLINE                    inline __attribute__((always_inline))

#endif
