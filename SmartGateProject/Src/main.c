#include "main.h"
#include "gpio.h"

/* Task Handles */
void vInputTask(void *pvParameters);
void vGateControlTask(void *pvParameters);
void vSafetyTask(void *pvParameters);
void vLEDTask(void *pvParameters);

int main(void)
{
    GPIO_Init();

    /* Create Queue */
    xButtonQueue = xQueueCreate(10, sizeof(ButtonEvent_t));

    /* Create Semaphores */
    xOpenLimitSem   = xSemaphoreCreateBinary();
    xClosedLimitSem = xSemaphoreCreateBinary();
		xObstacleSem = xSemaphoreCreateBinary();

    /* Create Mutex */
    xGateStateMutex = xSemaphoreCreateMutex();

    /* Create Tasks */
    xTaskCreate(vInputTask, "Input", 128, NULL, 3, NULL);
    xTaskCreate(vGateControlTask, "Gate", 128, NULL, 2, NULL);
    xTaskCreate(vLEDTask, "LED", 128, NULL, 2, NULL);
    xTaskCreate(vSafetyTask, "Safety", 128, NULL, 4, NULL);

    /* Start Scheduler */
    vTaskStartScheduler();

    while(1);
}