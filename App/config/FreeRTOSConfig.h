/*
 * FreeRTOS Kernel V11.3.1
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Project configuration:
 *   MCU       : GD32F470VET6 (Cortex-M4F, ARMv7E-M)
 *   Toolchain : Keil ARM Compiler 5 (AC5)
 *   Port      : portable/RVDS/ARM_CM4F
 *   Clock     : 240 MHz (SystemCoreClock)
 *   Memory    : Static allocation only
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include "gd32f4xx.h"

/*============================================================================
 * 1. Kernel clock and scheduling
 *===========================================================================*/

/* SysTick uses the processor clock. One RTOS tick is 1 ms. */
#define configCPU_CLOCK_HZ                      ((uint32_t)SystemCoreClock)
#define configTICK_RATE_HZ                      ((TickType_t)1000U)
#define configTICK_TYPE_WIDTH_IN_BITS           TICK_TYPE_WIDTH_32_BITS

#define configNUMBER_OF_CORES                   1
#define configUSE_PREEMPTION                    1
#define configUSE_TIME_SLICING                  1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION 1
#define configUSE_TICKLESS_IDLE                 0
#define configIDLE_SHOULD_YIELD                 1

/* Valid application task priorities: 0 to 7. Priority 0 is used by IdleTask. */
#define configMAX_PRIORITIES                    8
#define configMINIMAL_STACK_SIZE                128U
#define configMAX_TASK_NAME_LEN                 16
#define configSTACK_DEPTH_TYPE                  size_t

/* Store the high stack boundary in each TCB for VSCode RTOS stack analysis. */
#define configRECORD_STACK_HIGH_ADDRESS         1

/*============================================================================
 * 2. Static memory policy
 *===========================================================================*/

/* All tasks and kernel objects are created from application-owned RAM. */
#define configSUPPORT_STATIC_ALLOCATION         1
#define configSUPPORT_DYNAMIC_ALLOCATION        0

/* freertos_static.c supplies IdleTask and TimerTask TCB/stack memory. */
#define configKERNEL_PROVIDED_STATIC_MEMORY     0

/* No heap_x.c and no configTOTAL_HEAP_SIZE are used by this project. */
#define configUSE_NEWLIB_REENTRANT              0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

/*============================================================================
 * 3. Tasks, queues and synchronization
 *===========================================================================*/

#define configUSE_TASK_NOTIFICATIONS            1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES   1

#define configUSE_MUTEXES                       1
#define configUSE_RECURSIVE_MUTEXES             0
#define configUSE_COUNTING_SEMAPHORES           1
#define configUSE_QUEUE_SETS                    0

#define configUSE_EVENT_GROUPS                  1
#define configUSE_STREAM_BUFFERS                0

/* Allows queues and semaphores to be named in a kernel-aware debugger. */
#define configQUEUE_REGISTRY_SIZE               16

/*============================================================================
 * 4. Software timer service
 *===========================================================================*/

#define configUSE_TIMERS                        1
#define configTIMER_TASK_PRIORITY               4
#define configTIMER_TASK_STACK_DEPTH            256U
#define configTIMER_QUEUE_LENGTH                8
#define configUSE_DAEMON_TASK_STARTUP_HOOK      0

/*============================================================================
 * 5. Cortex-M4F interrupt priorities
 *===========================================================================*/

/* GD32F470 implements four NVIC priority bits: logical priorities 0 to 15. */
#define configPRIO_BITS                         __NVIC_PRIO_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

/* PendSV and SysTick run at the lowest configurable interrupt priority. */
#define configKERNEL_INTERRUPT_PRIORITY         0xF0

/*
 * ISR priority rule when NVIC_PRIGROUP_PRE4_SUB0 is selected:
 *   0 to 4  : must not call FreeRTOS FromISR APIs.
 *   5 to 15 : may call FreeRTOS FromISR APIs.
 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    0x50

/* Route the application vector table directly to the FreeRTOS port handlers. */
#define vPortSVCHandler                         SVC_Handler
#define xPortPendSVHandler                      PendSV_Handler
#define xPortSysTickHandler                     SysTick_Handler
#define configCHECK_HANDLER_INSTALLATION        1

/*============================================================================
 * 6. Hooks, assertions and diagnostics
 *===========================================================================*/

#define configUSE_IDLE_HOOK                     0
#define configUSE_TICK_HOOK                     0
#define configUSE_MALLOC_FAILED_HOOK            0

/* freertos_static.c provides vApplicationStackOverflowHook(). */
#define configCHECK_FOR_STACK_OVERFLOW          2

#define configUSE_TRACE_FACILITY                1

/*
 * Runtime statistics use the Cortex-M4 DWT cycle counter. The 32-bit hardware
 * counter is accumulated into a 64-bit software counter in freertos_static.c.
 */
void freertos_runtime_stats_init(void);
uint64_t freertos_runtime_stats_get(void);

#define configGENERATE_RUN_TIME_STATS           1
#define configRUN_TIME_COUNTER_TYPE             uint64_t
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() freertos_runtime_stats_init()
#define portGET_RUN_TIME_COUNTER_VALUE()        freertos_runtime_stats_get()

#define configUSE_STATS_FORMATTING_FUNCTIONS    0

#define configASSERT(condition)                 \
    do {                                        \
        if ((condition) == 0) {                 \
            __disable_irq();                    \
            for (;;) {                          \
            }                                   \
        }                                       \
    } while (0)

/*============================================================================
 * 7. Optional kernel features
 *===========================================================================*/

#define configENABLE_BACKWARD_COMPATIBILITY    0
#define configUSE_MINI_LIST_ITEM                1
#define configUSE_CO_ROUTINES                   0
#define configMAX_CO_ROUTINE_PRIORITIES         1
#define configUSE_APPLICATION_TASK_TAG          0
#define configUSE_POSIX_ERRNO                   0

/*============================================================================
 * 8. API inclusion
 *===========================================================================*/

#define INCLUDE_vTaskPrioritySet                0
#define INCLUDE_uxTaskPriorityGet               1
#define INCLUDE_vTaskDelete                     0
#define INCLUDE_vTaskSuspend                    1

#define INCLUDE_xTaskDelayUntil                 1
#define INCLUDE_xTaskDelay                      1
#define INCLUDE_xTaskGetSchedulerState          1
#define INCLUDE_xTaskGetCurrentTaskHandle       1

/* Use the V11 API whose return type follows configSTACK_DEPTH_TYPE. */
#define INCLUDE_uxTaskGetStackHighWaterMark     0
#define INCLUDE_uxTaskGetStackHighWaterMark2    1

#define INCLUDE_xTaskGetIdleTaskHandle          0
#define INCLUDE_eTaskGetState                   0
#define INCLUDE_xTimerPendFunctionCall          0
#define INCLUDE_xTaskAbortDelay                 0
#define INCLUDE_xTaskGetHandle                  0
#define INCLUDE_xTaskResumeFromISR              0

#endif /* FREERTOS_CONFIG_H */
