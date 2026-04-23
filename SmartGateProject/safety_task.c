#include "shared_types.h"
#include "gpio.h"

void vSafetyTask(void *pvParameters)
{
    while(1)
    {
        if (Read_Obstacle_Button())
        {
            xSemaphoreTake(xGateStateMutex, portMAX_DELAY);

            if (gateState == CLOSING)
            {
                Motor_Stop();

                Motor_Open();
                gateState = REVERSING;

                vTaskDelay(pdMS_TO_TICKS(500));

                Motor_Stop();
                gateState = STOPPED_MIDWAY;
            }

            xSemaphoreGive(xGateStateMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}