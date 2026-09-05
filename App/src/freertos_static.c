#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

/*
 * IdleTask静态内存。
 *
 * 128 words × 4字节 = 512字节。
 */
static StaticTask_t s_idle_task_tcb;

static StackType_t
    s_idle_task_stack[configMINIMAL_STACK_SIZE];

/*
 * Timer Service Task静态内存。
 *
 * 256 words × 4字节 = 1024字节。
 */
static StaticTask_t s_timer_task_tcb;

static StackType_t
    s_timer_task_stack[configTIMER_TASK_STACK_DEPTH];

/*
 * FreeRTOS runtime statistics counter.
 *
 * DWT->CYCCNT is a 32-bit CPU cycle counter. At 240 MHz it wraps about every
 * 17.9 seconds. Unsigned subtraction handles one wrap between samples, and the
 * elapsed cycles are accumulated into a 64-bit value for long debug sessions.
 */
static uint32_t s_runtime_last_cycle;
static uint64_t s_runtime_total_cycles;
static uint8_t s_runtime_counter_enabled;

void freertos_runtime_stats_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    if ((DWT->CTRL & DWT_CTRL_NOCYCCNT_Msk) != 0U) {
        s_runtime_counter_enabled = 0U;
        return;
    }

    DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0U;

    s_runtime_last_cycle = 0U;
    s_runtime_total_cycles = 0ULL;
    s_runtime_counter_enabled = 1U;

    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint64_t freertos_runtime_stats_get(void)
{
    uint32_t current_cycle;
    uint32_t elapsed_cycles;

    if (s_runtime_counter_enabled == 0U) {
        return 0ULL;
    }

    current_cycle = DWT->CYCCNT;
    elapsed_cycles = current_cycle - s_runtime_last_cycle;
    s_runtime_last_cycle = current_cycle;
    s_runtime_total_cycles += (uint64_t)elapsed_cycles;

    return s_runtime_total_cycles;
}

void vApplicationGetIdleTaskMemory(StaticTask_t **ppx_idle_task_tcb,
                                   StackType_t **ppx_idle_task_stack,
                                   configSTACK_DEPTH_TYPE *pux_idle_task_stack_size)
{
    *ppx_idle_task_tcb = &s_idle_task_tcb;
    *ppx_idle_task_stack = s_idle_task_stack;
    *pux_idle_task_stack_size = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory(StaticTask_t **ppx_timer_task_tcb,
                                    StackType_t **ppx_timer_task_stack,
                                    configSTACK_DEPTH_TYPE *pux_timer_task_stack_size)
{
    *ppx_timer_task_tcb = &s_timer_task_tcb;
    *ppx_timer_task_stack = s_timer_task_stack;
    *pux_timer_task_stack_size = configTIMER_TASK_STACK_DEPTH;
}

void vApplicationStackOverflowHook(
    TaskHandle_t task_handle,
    char *task_name)
{
    (void)task_handle;
    (void)task_name;

    taskDISABLE_INTERRUPTS();

    for (;;) {
    }
}
