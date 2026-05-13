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
 * TASK 1: vInputTask  (Priority 3)
 *
 * Reads raw ISR events from xButtonQueue.
 * The ISR already determines press type based on PF0 state:
 *   PF0 held (low)  -> PRESS_MANUAL   -> gate moves while held
 *   PF0 not held    -> PRESS_ONETOUCH -> gate moves to limit
 *   Button released -> PRESS_RELEASED -> forwarded as-is
 * vInputTask forwards all events to xGateEventQueue unchanged.
 * ===================================================================== */
void vInputTask(void *pvParameters)
{
    (void)pvParameters;
    ButtonEvent_t ev;

    /* Create the internal processed-event queue */
    xGateEventQueue = xQueueCreate(10, sizeof(ButtonEvent_t));

    vPrintString("System started. Gate state: IDLE_CLOSED\n");

    for (;;)
    {
        /* Block indefinitely until ISR posts an event */
        if (xQueueReceive(xButtonQueue, &ev, portMAX_DELAY) == pdTRUE)
        {
            /* Limit/obstacle use semaphores, not this queue. Discard. */
            if (ev.button == BTN_OPEN_LIMIT  ||
                ev.button == BTN_CLOSED_LIMIT ||
                ev.button == BTN_OBSTACLE)
                continue;

            /* Forward all events to Gate Control Task.
               ISR already set the correct press type:
               PRESS_MANUAL, PRESS_ONETOUCH, or PRESS_RELEASED. */
            xQueueSend(xGateEventQueue, &ev, 0);
        }
    }
}

/* =======================================================================
 * TASK 2: vGateControlTask  (Priority 2)
 *
 * Implements the full Gate State Machine from the project spec.
 *
 * States:
 *   IDLE_CLOSED    -> OPENING        (OPEN button)
 *   OPENING        -> IDLE_OPEN      (open limit semaphore)
 *   OPENING        -> STOPPED_MIDWAY (PRESS_RELEASED in manual mode)
 *   OPENING        -> STOPPED_MIDWAY (same panel CLOSE = conflict)
 *   OPENING        -> CLOSING        (Security CLOSE = override)
 *   IDLE_OPEN      -> CLOSING        (CLOSE button)
 *   CLOSING        -> IDLE_CLOSED    (closed limit semaphore)
 *   CLOSING        -> STOPPED_MIDWAY (PRESS_RELEASED in manual mode)
 *   CLOSING        -> STOPPED_MIDWAY (same panel OPEN = conflict)
 *   CLOSING        -> OPENING        (Security OPEN = override)
 *   CLOSING        -> STOPPED_MIDWAY -> REVERSING -> STOPPED_MIDWAY (obstacle)
 *   STOPPED_MIDWAY -> OPENING/CLOSING (any open/close button)
 *
 * Manual mode:    PRESS_MANUAL starts movement, PRESS_RELEASED stops it.
 * One-touch mode: PRESS_ONETOUCH starts movement, gate moves to limit.
 * Security priority:
 *   - Driver events ignored while bSecurityActive = true
 *   - Security opposite direction = immediate override
 *   - Same panel OPEN + CLOSE = conflict -> STOPPED_MIDWAY
 * ===================================================================== */
static volatile bool bManualMode     = false;  /* true while gate moving in manual mode */
static volatile bool bSecurityActive = false;

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

            /* PRESS_RELEASED: stop gate if in manual mode */
            if (ev.pressType == PRESS_RELEASED)
            {
                if (ev.panel == PANEL_SECURITY)
                    bSecurityActive = false;

                if (bManualMode)
                {
                    if (current == OPENING || current == CLOSING)
                    {
                        bManualMode = false;
                        GateState_Set(STOPPED_MIDWAY);
                        if (ev.panel == PANEL_DRIVER)
                            vPrintString("Gate state -> STOPPED_MIDWAY (button released) [Driver]\n");
                        else
                            vPrintString("Gate state -> STOPPED_MIDWAY (button released) [Security]\n");
                    }
                }
                /* One-touch mode: release ignored, gate keeps moving to limit */
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
                        bManualMode = (ev.pressType == PRESS_MANUAL);
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
                        bManualMode = (ev.pressType == PRESS_MANUAL);
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
                            /* Security overrides -> go directly to CLOSING */
                            bManualMode = (ev.pressType == PRESS_MANUAL);
                            GateState_Set(CLOSING);
                            vPrintString("Gate state -> CLOSING (Red LED ON) [Security CLOSE overrides OPENING]\n");
                        }
                        else
                        {
                            /* Same panel conflict -> safe stop */
                            bManualMode = false;
                            GateState_Set(STOPPED_MIDWAY);
                            vPrintString("Gate state -> STOPPED_MIDWAY (conflicting input) [Driver CLOSE while OPENING]\n");
                        }
                    }
                    break;

                /* CLOSING: gate moving down */
                case CLOSING:
                    if (ev.button == BTN_DRV_OPEN ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        if (ev.panel == PANEL_SECURITY)
                        {
                            /* Security overrides -> go directly to OPENING */
                            bManualMode = (ev.pressType == PRESS_MANUAL);
                            GateState_Set(OPENING);
                            vPrintString("Gate state -> OPENING (Green LED ON) [Security OPEN overrides CLOSING]\n");
                        }
                        else
                        {
                            /* Same panel conflict -> safe stop */
                            bManualMode = false;
                            GateState_Set(STOPPED_MIDWAY);
                            vPrintString("Gate state -> STOPPED_MIDWAY (conflicting input) [Driver OPEN while CLOSING]\n");
                        }
                    }
                    break;

                /* STOPPED_MIDWAY: gate stopped between limits */
                case STOPPED_MIDWAY:
                    if (ev.button == BTN_DRV_OPEN ||
                        ev.button == BTN_SEC_OPEN)
                    {
                        bManualMode = (ev.pressType == PRESS_MANUAL);
                        GateState_Set(OPENING);
                        if (ev.panel == PANEL_DRIVER)
                            vPrintString("Gate state -> OPENING (Green LED ON) [Driver OPEN from STOPPED]\n");
                        else
                            vPrintString("Gate state -> OPENING (Green LED ON) [Security OPEN from STOPPED]\n");
                    }
                    else if (ev.button == BTN_DRV_CLOSE ||
                             ev.button == BTN_SEC_CLOSE)
                    {
                        bManualMode = (ev.pressType == PRESS_MANUAL);
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
                bManualMode     = false;
                bSecurityActive = false;
                GateState_Set(IDLE_OPEN);
                vPrintString("Gate state -> IDLE_OPEN (open limit reached, LEDs OFF)\n");
            }
        }
        else if (current == CLOSING)
        {
            if (xSemaphoreTake(xClosedLimitSem, 0) == pdTRUE)
            {
                bManualMode     = false;
                bSecurityActive = false;
                GateState_Set(IDLE_CLOSED);
                vPrintString("Gate state -> IDLE_CLOSED (closed limit reached, LEDs OFF)\n");
            }
        }
    }
}

/* =======================================================================
 * TASK 3: vSafetyTask  (Priority 4 - Highest)
 *
 * Blocks on xObstacleSem (given by PF4 ISR on press).
 * Only reacts if gate is CLOSING. Ignored in all other states.
 *
 * Sequence:
 *   CLOSING -> STOPPED_MIDWAY (stop, LEDs OFF, 100ms)
 *           -> REVERSING      (reverse, Green LED ON, 500ms)
 *           -> STOPPED_MIDWAY (stop completely, LEDs OFF)
 * ===================================================================== */
void vSafetyTask(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        if (xSemaphoreTake(xObstacleSem, portMAX_DELAY) == pdTRUE)
        {
            /* Only react if gate is currently CLOSING */
            if (GateState_CompareAndSet(CLOSING, STOPPED_MIDWAY))
            {
                /* Step 1: Stop immediately */
                vPrintString("Gate state -> STOPPED_MIDWAY (obstacle: gate stopped) [Obstacle]\n");
                vTaskDelay(pdMS_TO_TICKS(100));

                /* Step 2: Reverse (open direction) for 500ms */
                GateState_Set(REVERSING);
                vPrintString("Gate state -> REVERSING (obstacle: reversing, Green LED ON) [Obstacle]\n");
                vTaskDelay(pdMS_TO_TICKS(500));

                /* Step 3: Stop completely */
                GateState_Set(STOPPED_MIDWAY);
                vPrintString("Gate state -> STOPPED_MIDWAY (obstacle: reverse complete, LEDs OFF) [Obstacle]\n");
            }
            else
            {
                vPrintString("Obstacle detected but ignored (gate not closing) [Obstacle]\n");
            }
        }
    }
}

/* =======================================================================
 * TASK 4: vLEDTask  (Priority 2)
 *
 * Polls gate state every 50ms and drives LEDs using LED_Set/LED_AllOff
 * from main.c:
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
                LED_Set(LED_GREEN);    /* Green ON: gate moving up   */
                break;

            case CLOSING:
                LED_Set(LED_RED);      /* Red ON: gate moving down   */
                break;

            case IDLE_OPEN:
            case IDLE_CLOSED:
            case STOPPED_MIDWAY:
            default:
                LED_AllOff();          /* Both OFF: gate stationary  */
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}