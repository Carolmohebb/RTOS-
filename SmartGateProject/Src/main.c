#include "tm4c123gh6pm.h"
#include "main.h"
#include "gpio.h"

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

static void GateState_Set(GateState_t newState)
{
    xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
    gateState = newState;
    xSemaphoreGive(xGateStateMutex);
}

static GateState_t GateState_Get(void)
{
    GateState_t state;
    xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
    state = gateState;
    xSemaphoreGive(xGateStateMutex);
    return state;
}

static bool GateState_CompareAndSet(GateState_t expected, GateState_t newState)
{
		bool success = false;
		xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
		if (gateState == expected)
		{
				gateState = newState;
				success = true;
		}
		xSemaphoreGive(xGateStateMutex);
		return success;
}