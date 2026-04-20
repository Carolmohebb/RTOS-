#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include "gpio.h"
#include "shared_types.h"

QueueHandle_t        xButtonQueue;
SemaphoreHandle_t    xOpenLimitSem;
SemaphoreHandle_t    xClosedLimitSem;
SemaphoreHandle_t    xGateStateMutex;
volatile GateState_t gateState = IDLE_CLOSED;

void vTask1(void *pvParams) {
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

int main(void) {
    gpio_init();

    xButtonQueue    = xQueueCreate(10, sizeof(ButtonEvent_t));
    xOpenLimitSem   = xSemaphoreCreateBinary();
    xClosedLimitSem = xSemaphoreCreateBinary();
    xGateStateMutex = xSemaphoreCreateMutex();

    xTaskCreate(vTask1, "Task1", 128, NULL, 1, NULL);

    vTaskStartScheduler();
    for(;;);
}