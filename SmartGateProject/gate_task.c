#include "shared_types.h"
#include "gpio.h"

void vGateControlTask(void *pvParameters)
{
    ButtonEvent_t event;

    while(1)
    {
        if (xQueueReceive(xButtonQueue, &event, portMAX_DELAY))
        {
            xSemaphoreTake(xGateStateMutex, portMAX_DELAY);

            switch(gateState)
            {
                case IDLE_CLOSED:
                    if (event.button == BTN_DRV_OPEN)
                    {
                        Motor_Open();
                        gateState = OPENING;
                    }
                    break;

                case OPENING:
                    if (xSemaphoreTake(xOpenLimitSem, 0))
                    {
                        Motor_Stop();
                        gateState = IDLE_OPEN;
                    }
                    break;

                case IDLE_OPEN:
                    if (event.button == BTN_DRV_CLOSE)
                    {
                        Motor_Close();
                        gateState = CLOSING;
                    }
                    break;

                case CLOSING:
                    if (xSemaphoreTake(xClosedLimitSem, 0))
                    {
                        Motor_Stop();
                        gateState = IDLE_CLOSED;
                    }
                    break;

                default:
                    break;
            }

            xSemaphoreGive(xGateStateMutex);
        }
    }
}