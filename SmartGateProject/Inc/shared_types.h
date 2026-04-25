#ifndef SHARED_TYPES_H
#define SHARED_TYPES_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"

/* -- Gate States --------------------------------------- */
typedef enum {
    IDLE_CLOSED = 0,
    OPENING,
    IDLE_OPEN,
    CLOSING,
    STOPPED_MIDWAY,
    REVERSING
} GateState_t;

/* -- Button IDs ---------------------------------------- */
typedef enum {
    BTN_DRV_OPEN = 0,
    BTN_DRV_CLOSE,
    BTN_SEC_OPEN,
    BTN_SEC_CLOSE,
    BTN_OPEN_LIMIT,
    BTN_CLOSED_LIMIT,
    BTN_OBSTACLE
} ButtonID_t;

/* -- Panel Source -------------------------------------- */
typedef enum {
    PANEL_DRIVER,
    PANEL_SECURITY
} PanelID_t;

/* -- Press Type ---------------------------------------- */
typedef enum {
    PRESS_MANUAL,
    PRESS_ONETOUCH,
    PRESS_RELEASED
} PressType_t;

/* -- Event Struct -------------------------------------- */
typedef struct {
    ButtonID_t  button;
    PanelID_t   panel;
    PressType_t pressType;
} ButtonEvent_t;

/* -- Shared RTOS Objects ------------------------------- */
extern QueueHandle_t     xButtonQueue;
extern SemaphoreHandle_t xOpenLimitSem;
extern SemaphoreHandle_t xClosedLimitSem;
extern SemaphoreHandle_t xObstacleSem;
extern SemaphoreHandle_t xGateStateMutex;

extern GateState_t gateState;

#endif