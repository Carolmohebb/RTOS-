#ifndef MAIN_H
#define MAIN_H

#include "shared_types.h"
#include <stdint.h>
#include <stdbool.h>

void vInputTask(void *pvParameters);
void vGateControlTask(void *pvParameters);
void vSafetyTask(void *pvParameters);
void vLEDTask(void *pvParameters);

static void GateState_Set(GateState_t newState);
static GateState_t GateState_Get(void);
static bool GateState_CompareAndSet(GateState_t expected, GateState_t newState);

#endif
