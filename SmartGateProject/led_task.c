#include "shared_types.h"
#include "gpio.h"

void vLEDTask(void *pvParameters)
{
    while(1)
    {
        xSemaphoreTake(xGateStateMutex, portMAX_DELAY);

        if (gateState == OPENING)
        {
            Green_LED_On();
            Red_LED_Off();
        }
        else if (gateState == CLOSING)
        {
            Red_LED_On();
            Green_LED_Off();
        }
        else
        {
            Green_LED_Off();
            Red_LED_Off();
        }

        xSemaphoreGive(xGateStateMutex);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}