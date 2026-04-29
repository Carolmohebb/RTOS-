#include "tm4c123gh6pm.h"
#include "main.h"
#include "basic_io.h"

QueueHandle_t     xButtonQueue;
SemaphoreHandle_t xOpenLimitSem;
SemaphoreHandle_t xClosedLimitSem;
SemaphoreHandle_t xObstacleSem;
SemaphoreHandle_t xGateStateMutex;
GateState_t       gateState = IDLE_CLOSED;

/* Internal queue: vInputTask -> vGateControlTask only.
   ISRs post raw events to xButtonQueue.
   vInputTask reads xButtonQueue, processes, posts to xGateEventQueue.
   vGateControlTask reads xGateEventQueue only.
   This prevents either task from consuming the other's messages.        */
static QueueHandle_t xGateEventQueue;

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName;
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
 *         and re-post to xGateEventQueue.
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

    /* Create the internal processed-event queue */
    xGateEventQueue = xQueueCreate(10, sizeof(ButtonEvent_t));

    vPrintString("System started. Gate state: IDLE_CLOSED\n");

    for (;;)
    {
        vPrintString("Input task running...\n");
        /* Block indefinitely until ISR posts an event */
        if (xQueueReceive(xButtonQueue, &ev, portMAX_DELAY) == pdTRUE)
        {
            /* Limit/obstacle use semaphores, not this queue. Discard. */
            if (ev.button == BTN_OPEN_LIMIT  ||
                ev.button == BTN_CLOSED_LIMIT ||
                ev.button == BTN_OBSTACLE)
                continue;

            if (ev.pressType == PRESS_MANUAL)
            {
                /* Record press time */
                pressTime[ev.button] = xTaskGetTickCount();

                /* Forward PRESS_MANUAL immediately: gate starts moving */
                xQueueSend(xGateEventQueue, &ev, 0);
            }
            else if (ev.pressType == PRESS_RELEASED)
            {
                TickType_t held = xTaskGetTickCount() - pressTime[ev.button];

                if (held < pdMS_TO_TICKS(ONE_TOUCH_THRESHOLD_MS))
                {
                    /* Quick tap -> One-Touch */
                    ButtonEvent_t otEv = ev;
                    otEv.pressType     = PRESS_ONETOUCH;
                    xQueueSend(xGateEventQueue, &otEv, 0);
                }
                else
                {
                    /* Long hold released -> stop gate */
                    xQueueSend(xGateEventQueue, &ev, 0);
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
 *   OPENING        -> STOPPED_MIDWAY  (same panel CLOSE = conflict)
 *   OPENING        -> CLOSING         (Security CLOSE = override)
 *   IDLE_OPEN      -> CLOSING  (CLOSE button)
 *   CLOSING        -> IDLE_CLOSED     (closed limit semaphore)
 *   CLOSING        -> STOPPED_MIDWAY  (button released, manual mode)
 *   CLOSING        -> STOPPED_MIDWAY  (same panel OPEN = conflict)
 *   CLOSING        -> OPENING         (Security OPEN = override)
 *   CLOSING        -> REVERSING       (obstacle - handled by Safety Task)
 *   REVERSING      -> STOPPED_MIDWAY  (after 0.5s - handled by Safety Task)
 *   STOPPED_MIDWAY -> OPENING/CLOSING (any open/close button)
 *
 * Security priority:
 *   - Driver events ignored while bSecurityActive = true
 *   - Security pressing opposite direction while gate is moving
 *     overrides immediately (no conflict stop)
 *   - Same panel pressing OPEN + CLOSE simultaneously = conflict stop
 * ===================================================================== */

static volatile bool bAutoMode       = false;
static volatile bool bSecurityActive = false;
static volatile bool bConflictStop   = false;  /* set when same-panel conflict causes
                                                   STOPPED_MIDWAY. Causes the follow-up
                                                   release/onetouch event to be discarded
                                                   so gate stays stopped until new input. */

void vGateControlTask(void *pvParameters)
{
    (void)pvParameters;
    ButtonEvent_t ev;
    GateState_t   current;

    /* Wait until vInputTask has created xGateEventQueue */
    while (xGateEventQueue == NULL) { vTaskDelay(pdMS_TO_TICKS(10)); }

    for (;;)
    {
        /* 50ms timeout ensures limit poll always runs */
        if (xQueueReceive(xGateEventQueue, &ev, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            current = GateState_Get();

            /* Security priority: ignore Driver while Security is active */
            if (ev.panel == PANEL_DRIVER && bSecurityActive)
                goto check_limits;

            /* Discard the event that follows a same-panel conflict stop.
               Without this, the follow-up release/onetouch event moves
               the gate again immediately after the conflict stop.        */
            if (bConflictStop)
            {
                bConflictStop = false;
                if (ev.panel == PANEL_SECURITY)
                    bSecurityActive = false;
                goto check_limits;
            }

            /* PRESS_RELEASED: stop gate if in manual mode */
            if (ev.pressType == PRESS_RELEASED)
            {
                /* Clear security lock only when security panel releases */
                if (ev.panel == PANEL_SECURITY)
                    bSecurityActive = false;

                if (!bAutoMode)
                {
                    if (current == OPENING || current == CLOSING)
                    {
                        GateState_Set(STOPPED_MIDWAY);
                        if (ev.panel == PANEL_DRIVER)
                            vPrintString("Gate state -> STOPPED_MIDWAY (button released) [Driver]\n");
                        else
                            vPrintString("Gate state -> STOPPED_MIDWAY (button released) [Security]\n");
                    }
                }
                /* Auto mode: release ignored, gate keeps moving to limit */
                goto check_limits;
            }

            /* Any security press activates security lock */
            if (ev.panel == PANEL_SECURITY)
                bSecurityActive = true;

            /* State machine transitions */
            switch (current)
            {
                /* IDLE_CLOSED: gate fully closed, waiting for OPEN */
                case IDLE_CLOSED:
                    if (ev.button == BTN_DRV_OPEN ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(OPENING);
                        if (ev.panel == PANEL_DRIVER)
                            vPrintString("Gate state -> OPENING (Green LED ON) [Driver OPEN]\n");
                        else
                            vPrintString("Gate state -> OPENING (Green LED ON) [Security OPEN]\n");
                    }
                    break;

                /* IDLE_OPEN: gate fully open, waiting for CLOSE */
                case IDLE_OPEN:
                    if (ev.button == BTN_DRV_CLOSE ||
                        ev.button == BTN_SEC_CLOSE)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(CLOSING);
                        if (ev.panel == PANEL_DRIVER)
                            vPrintString("Gate state -> CLOSING (Red LED ON) [Driver CLOSE]\n");
                        else
                            vPrintString("Gate state -> CLOSING (Red LED ON) [Security CLOSE]\n");
                    }
                    break;

                /* OPENING: gate moving up */
                case OPENING:
                    if (ev.button == BTN_DRV_CLOSE ||
                        ev.button == BTN_SEC_CLOSE)
                    {
                        if (ev.panel == PANEL_SECURITY)
                        {
                            /* Security overrides Driver -> go directly to CLOSING */
                            bAutoMode     = (ev.pressType == PRESS_ONETOUCH);
                            bConflictStop = false;
                            GateState_Set(CLOSING);
                            vPrintString("Gate state -> CLOSING (Red LED ON) [Security CLOSE overrides OPENING]\n");
                        }
                        else
                        {
                            /* Same panel conflict (Driver OPEN + Driver CLOSE) -> safe stop */
                            bAutoMode     = false;
                            bConflictStop = true;
                            GateState_Set(STOPPED_MIDWAY);
                            vPrintString("Gate state -> STOPPED_MIDWAY (conflicting input) [Driver CLOSE while OPENING]\n");
                        }
                    }
                    else if (ev.button == BTN_DRV_OPEN ||
                             ev.button == BTN_SEC_OPEN)
                    {
                        /* Already opening, update auto/manual mode */
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                    }
                    break;

                /* CLOSING: gate moving down */
                case CLOSING:
                    if (ev.button == BTN_DRV_OPEN ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        if (ev.panel == PANEL_SECURITY)
                        {
                            /* Security overrides Driver -> go directly to OPENING */
                            bAutoMode     = (ev.pressType == PRESS_ONETOUCH);
                            bConflictStop = false;
                            GateState_Set(OPENING);
                            vPrintString("Gate state -> OPENING (Green LED ON) [Security OPEN overrides CLOSING]\n");
                        }
                        else
                        {
                            /* Same panel conflict (Driver CLOSE + Driver OPEN) -> safe stop */
                            bAutoMode     = false;
                            bConflictStop = true;
                            GateState_Set(STOPPED_MIDWAY);
                            vPrintString("Gate state -> STOPPED_MIDWAY (conflicting input) [Driver OPEN while CLOSING]\n");
                        }
                    }
                    else if (ev.button == BTN_DRV_CLOSE ||
                             ev.button == BTN_SEC_CLOSE)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                    }
                    break;

                /* STOPPED_MIDWAY: gate stopped between limits */
                case STOPPED_MIDWAY:
                    if (ev.button == BTN_DRV_OPEN ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(OPENING);
                        if (ev.panel == PANEL_DRIVER)
                            vPrintString("Gate state -> OPENING (Green LED ON) [Driver OPEN from STOPPED]\n");
                        else
                            vPrintString("Gate state -> OPENING (Green LED ON) [Security OPEN from STOPPED]\n");
                    }
                    else if (ev.button == BTN_DRV_CLOSE ||
                             ev.button == BTN_SEC_CLOSE)
                    {
                        bAutoMode = (ev.pressType == PRESS_ONETOUCH);
                        GateState_Set(CLOSING);
                        if (ev.panel == PANEL_DRIVER)
                            vPrintString("Gate state -> CLOSING (Red LED ON) [Driver CLOSE from STOPPED]\n");
                        else
                            vPrintString("Gate state -> CLOSING (Red LED ON) [Security CLOSE from STOPPED]\n");
                    }
                    break;

                /* REVERSING: owned entirely by Safety Task */
                case REVERSING:
                    break;

                default:
                    break;
            }
        }

check_limits:
        /* Non-blocking limit semaphore poll (runs every 50ms).
           Handles limit stops in both manual and auto mode.             */
        current = GateState_Get();

        if (current == OPENING)
        {
            if (xSemaphoreTake(xOpenLimitSem, 0) == pdTRUE)
            {
                bAutoMode       = false;
                bSecurityActive = false;
                GateState_Set(IDLE_OPEN);
                vPrintString("Gate state -> IDLE_OPEN (open limit reached, LEDs OFF)\n");
            }
        }
        else if (current == CLOSING)
        {
            if (xSemaphoreTake(xClosedLimitSem, 0) == pdTRUE)
            {
                bAutoMode       = false;
                bSecurityActive = false;
                GateState_Set(IDLE_CLOSED);
                vPrintString("Gate state -> IDLE_CLOSED (closed limit reached, LEDs OFF)\n");
            }
        }
    }
}

/* =======================================================================
 * TASK 3: vSafetyTask  (Highest priority)
 *
 * Blocks on xObstacleSem. When signalled:
 *   1. Only acts if gate is currently CLOSING.
 *   2. Sets state to REVERSING (Green LED ON via LED task).
 *   3. Waits 500ms.
 *   4. Sets state to STOPPED_MIDWAY.
 * ===================================================================== */
void vSafetyTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        if (xSemaphoreTake(xObstacleSem, portMAX_DELAY) == pdTRUE)
        {
            if (GateState_CompareAndSet(CLOSING, REVERSING))
            {
                vPrintString("Gate state -> REVERSING (obstacle detected, Green LED ON) [Obstacle]\n");

                vTaskDelay(pdMS_TO_TICKS(500));

                GateState_Set(STOPPED_MIDWAY);
                vPrintString("Gate state -> STOPPED_MIDWAY (reverse complete, LEDs OFF) [Obstacle]\n");
            }
            else
            {
                vPrintString("Obstacle detected but ignored (gate not closing) [Obstacle]\n");
            }
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
                GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~LED_MASK) | LED_GREEN;
                break;

            case CLOSING:
                GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~LED_MASK) | LED_RED;
                break;

            default:
                LED_AllOff();
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}