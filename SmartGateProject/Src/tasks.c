#include "tm4c123gh6pm.h"
#include "main.h"

QueueHandle_t     xButtonQueue;
SemaphoreHandle_t xOpenLimitSem;
SemaphoreHandle_t xClosedLimitSem;
SemaphoreHandle_t xObstacleSem;
SemaphoreHandle_t xGateStateMutex;
GateState_t       gateState = IDLE_CLOSED;

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
    /* Trap here on stack overflow */
    for (;;) {}
}

/* =======================================================================
 * TASK 1: vInputTask
 *
 * With interrupt-driven design, the ISRs post directly to xButtonQueue
 * and the limit/obstacle semaphores. The Input Task's role is therefore
 * to handle ONE-TOUCH detection:
 *
 *   - On PRESS_MANUAL arriving from ISR, start a timer.
 *   - On PRESS_RELEASED:
 *       * if held time < ONE_TOUCH_THRESHOLD_MS -> convert to PRESS_ONETOUCH
 *         and re-post to a second internal queue (or re-use xButtonQueue).
 *       * else the PRESS_RELEASED event is forwarded as-is (manual stop).
 *
 * This keeps all RTOS logic out of the ISR while still being interrupt-
 * driven at the hardware level.
 * ===================================================================== */

#define ONE_TOUCH_THRESHOLD_MS  300U

/* We track press timestamps per button (7 buttons max) */
static TickType_t pressTime[7] = {0};   /* indexed by ButtonID_t */

void vInputTask(void *pvParameters)
{
    (void)pvParameters;
    ButtonEvent_t ev;

    for (;;)
    {
        /* Block indefinitely until ISR posts an event */
        if (xQueueReceive(xButtonQueue, &ev, portMAX_DELAY) == pdTRUE)
        {
            /* Limit buttons and obstacle go directly to their semaphores
             * in the ISR - nothing to do here for those.
             * We only process OPEN/CLOSE button events. */
            if (ev.button == BTN_OPEN_LIMIT  ||
                ev.button == BTN_CLOSED_LIMIT ||
                ev.button == BTN_OBSTACLE)
            {
                /* These should not arrive in the queue - they use semaphores.
                 * Discard defensively. */
                continue;
            }

            if (ev.pressType == PRESS_MANUAL)
            {
                /* Record press time - gate control will receive this as MANUAL */
                pressTime[ev.button] = xTaskGetTickCount();

                /* Forward the press event immediately so gate starts moving */
                xQueueSend(xButtonQueue, &ev, 0);
            }
            else if (ev.pressType == PRESS_RELEASED)
            {
                TickType_t held = xTaskGetTickCount() - pressTime[ev.button];

                if (held < pdMS_TO_TICKS(ONE_TOUCH_THRESHOLD_MS))
                {
                    /* Short press -> One-Touch: gate moves to limit autonomously */
                    ButtonEvent_t otEv = ev;
                    otEv.pressType = PRESS_ONETOUCH;
                    xQueueSend(xButtonQueue, &otEv, 0);
                }
                else
                {
                    /* Long press released -> Manual stop */
                    xQueueSend(xButtonQueue, &ev, 0);
                }
            }
        }
    }
}

/* =======================================================================
 * TASK 2: vGateControlTask
 *
 * Implements the full Gate State Machine from the project spec.
 *
 * States:
 *   IDLE_CLOSED    -> OPENING  (OPEN button)
 *   OPENING        -> IDLE_OPEN       (open limit semaphore)
 *   OPENING        -> STOPPED_MIDWAY  (button released, manual mode)
 *   IDLE_OPEN      -> CLOSING  (CLOSE button)
 *   CLOSING        -> IDLE_CLOSED     (closed limit semaphore)
 *   CLOSING        -> STOPPED_MIDWAY  (button released, manual mode)
 *   CLOSING        -> REVERSING       (obstacle - handled by Safety Task)
 *   REVERSING      -> STOPPED_MIDWAY  (after 0.5s - handled by Safety Task)
 *   STOPPED_MIDWAY -> OPENING/CLOSING (any open/close button)
 *
 * Security panel priority: if a Security event arrives, it overrides
 * any conflicting Driver event already being processed.
 * ===================================================================== */

/* Internal flags set by Gate Control to know which mode is active */
static volatile bool bAutoMode      = false;   /* true = one-touch, gate moves to limit */
static volatile bool bSecurityActive = false;  /* true = security command in control    */

void vGateControlTask(void *pvParameters)
{
    (void)pvParameters;
    ButtonEvent_t ev;
    GateState_t   current;

    for (;;)
    {
        /* Wait for a button event (with a short timeout so we can also
         * poll limit semaphores in auto mode without missing events) */
        if (xQueueReceive(xButtonQueue, &ev, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            current = GateState_Get();

            /* --------------------------------------------------------
             * Security priority:
             * If a Driver event arrives while a Security command is
             * active, ignore the driver event.
             * -------------------------------------------------------- */
            if (ev.panel == PANEL_DRIVER && bSecurityActive)
                continue;

            if (ev.panel == PANEL_SECURITY)
                bSecurityActive = true;
            else
                bSecurityActive = false;

            /* --------------------------------------------------------
             * PRESS_RELEASED in Manual mode -> stop gate
             * -------------------------------------------------------- */
            if (ev.pressType == PRESS_RELEASED)
            {
                if (!bAutoMode)
                {
                    /* Only stop if we are currently moving */
                    if (current == OPENING || current == CLOSING)
                    {
                        GateState_Set(STOPPED_MIDWAY);
                    }
                }
                /* In auto mode, release is ignored - gate keeps moving */
                bSecurityActive = false;
                continue;
            }

            /* --------------------------------------------------------
             * Conflicting inputs on same panel: OPEN + CLOSE
             * Both arrive in close succession -> safe stop
             * We detect this by checking if opposite button event is
             * already in the queue.
             * -------------------------------------------------------- */
            /* (Handled implicitly: if OPENING and a CLOSE arrives,
             *  we check below and transition to STOPPED_MIDWAY) */

            /* --------------------------------------------------------
             * State Machine transitions
             * -------------------------------------------------------- */
            switch (current)
            {
                /* -- IDLE_CLOSED: only OPEN starts movement -- */
                case IDLE_CLOSED:
                    if (ev.button == BTN_DRV_OPEN  ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(OPENING);
                    }
                    break;

                /* -- IDLE_OPEN: only CLOSE starts movement -- */
                case IDLE_OPEN:
                    if (ev.button == BTN_DRV_CLOSE ||
                        ev.button == BTN_SEC_CLOSE)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(CLOSING);
                    }
                    break;

                /* -- OPENING: CLOSE button = conflicting -> safe stop
                 *             OPEN limit  = handled by limit semaphore below -- */
                case OPENING:
                    if (ev.button == BTN_DRV_CLOSE ||
                        ev.button == BTN_SEC_CLOSE)
                    {
                        /* Conflicting input -> stop */
                        bAutoMode = false;
                        GateState_Set(STOPPED_MIDWAY);
                    }
                    else if (ev.button == BTN_DRV_OPEN ||
                             ev.button == BTN_SEC_OPEN)
                    {
                        /* Already opening, update mode if security overrides */
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                    }
                    break;

                /* -- CLOSING: OPEN button = conflicting -> safe stop -- */
                case CLOSING:
                    if (ev.button == BTN_DRV_OPEN  ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        bAutoMode = false;
                        GateState_Set(STOPPED_MIDWAY);
                    }
                    else if (ev.button == BTN_DRV_CLOSE ||
                             ev.button == BTN_SEC_CLOSE)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                    }
                    break;

                /* -- STOPPED_MIDWAY: any direction restarts -- */
                case STOPPED_MIDWAY:
                    if (ev.button == BTN_DRV_OPEN  ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(OPENING);
                    }
                    else if (ev.button == BTN_DRV_CLOSE ||
                             ev.button == BTN_SEC_CLOSE)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(CLOSING);
                    }
                    break;

                /* -- REVERSING: controlled entirely by Safety Task -- */
                case REVERSING:
                    break;

                default:
                    break;
            }
        }

        /* ------------------------------------------------------------
         * Limit semaphore checks (non-blocking poll)
         * Run every 50ms timeout cycle regardless of queue events.
         * ------------------------------------------------------------ */
        current = GateState_Get();

        if (current == OPENING)
        {
            if (xSemaphoreTake(xOpenLimitSem, 0) == pdTRUE)
            {
                /* Gate reached fully open position */
                bAutoMode       = false;
                bSecurityActive = false;
                GateState_Set(IDLE_OPEN);
            }
        }
        else if (current == CLOSING)
        {
            if (xSemaphoreTake(xClosedLimitSem, 0) == pdTRUE)
            {
                /* Gate reached fully closed position */
                bAutoMode       = false;
                bSecurityActive = false;
                GateState_Set(IDLE_CLOSED);
            }
        }
    }
}

/* =======================================================================
 * TASK 3: vSafetyTask  (Highest priority)
 *
 * Blocks on xObstacleSem. When signalled:
 *   1. Only acts if gate is currently CLOSING.
 *   2. Sets state to REVERSING (Green LED = ON via LED task).
 *   3. Waits 500ms.
 *   4. Sets state to STOPPED_MIDWAY.
 * ===================================================================== */
void vSafetyTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        /* Block until obstacle ISR gives the semaphore */
        if (xSemaphoreTake(xObstacleSem, portMAX_DELAY) == pdTRUE)
        {
            /* Only react if gate is currently closing */
            if (GateState_CompareAndSet(CLOSING, REVERSING))
            {
                /* LED task will see REVERSING and turn Green ON */
                vTaskDelay(pdMS_TO_TICKS(500));

                /* Stop after 0.5s reverse */
                GateState_Set(STOPPED_MIDWAY);
            }
            /* If gate was not closing, obstacle is ignored (e.g. during opening) */
        }
    }
}

/* =======================================================================
 * TASK 4: vLEDTask
 *
 * Reads gate state every 50ms and drives LEDs accordingly:
 *   OPENING  / REVERSING -> Green ON,  Red OFF
 *   CLOSING              -> Red ON,    Green OFF
 *   All others           -> Both OFF
 * ===================================================================== */
void vLEDTask(void *pvParameters)
{
    (void)pvParameters;
    GateState_t current;

    for (;;)
    {
        current = GateState_Get();

        switch (current)
        {
            case OPENING:
            case REVERSING:
                /* Green ON, Red OFF */
                GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~LED_MASK) | LED_GREEN;
                break;

            case CLOSING:
                /* Red ON, Green OFF */
                GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~LED_MASK) | LED_RED;
                break;

            default:
                /* IDLE_OPEN, IDLE_CLOSED, STOPPED_MIDWAY -> both OFF */
                LED_AllOff();
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}