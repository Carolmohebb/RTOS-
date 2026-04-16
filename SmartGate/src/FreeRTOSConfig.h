#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#define configUSE_PREEMPTION                1
#define configUSE_IDLE_HOOK                 0
#define configUSE_TICK_HOOK                 0
#define configCPU_CLOCK_HZ                  ( 80000000UL )
#define configTICK_RATE_HZ                  ( 1000 )
#define configMAX_PRIORITIES                ( 5 )
#define configMINIMAL_STACK_SIZE            ( 128 )
#define configTOTAL_HEAP_SIZE               ( 10240 )
#define configMAX_TASK_NAME_LEN             ( 16 )
#define configUSE_TRACE_FACILITY            0
#define configUSE_16_BIT_TICKS              0
#define configIDLE_SHOULD_YIELD             1
#define configUSE_MUTEXES                   1
#define configUSE_COUNTING_SEMAPHORES       0
#define configQUEUE_REGISTRY_SIZE           8
#define configUSE_RECURSIVE_MUTEXES         0
#define configUSE_TIMERS                    0
#define configSUPPORT_DYNAMIC_ALLOCATION    1
#define configSUPPORT_STATIC_ALLOCATION     0
#define INCLUDE_vTaskDelay                  1
#define INCLUDE_vTaskDelayUntil             1
#define INCLUDE_xSemaphoreGetMutexHolder    1
#define INCLUDE_uxTaskGetStackHighWaterMark 1
#define configPRIO_BITS                     3
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         0x07
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    0x04
#define configKERNEL_INTERRUPT_PRIORITY     ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

#endif