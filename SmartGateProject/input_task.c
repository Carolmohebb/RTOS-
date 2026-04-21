#include "shared_types.h"
#include "gpio.h"

void vInputTask(void *pvParameters)
{
    ButtonEvent_t event;

    while(1)
    {
        if (Read_Open_Button())
        {
            event.button = BTN_DRV_OPEN;
            event.panel = PANEL_DRIVER;
            event.pressType = PRESS_MANUAL;

            xQueueSend(xButtonQueue, &event, 0);
        }

        if (Read_Close_Button())
        {
            event.button = BTN_DRV_CLOSE;
            event.panel = PANEL_DRIVER;
            event.pressType = PRESS_MANUAL;

            xQueueSend(xButtonQueue, &event, 0);
        }

        if (Read_Open_Limit())
            xSemaphoreGive(xOpenLimitSem);

        if (Read_Closed_Limit())
            xSemaphoreGive(xClosedLimitSem);

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}