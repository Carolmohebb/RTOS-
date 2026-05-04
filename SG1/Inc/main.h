#ifndef MAIN_H
#define MAIN_H

#include "shared_types.h"

void vInputTask(void *pvParameters);
void vGateControlTask(void *pvParameters);
void vSafetyTask(void *pvParameters);
void vLEDTask(void *pvParameters);

void GateState_Set(GateState_t newState);
GateState_t GateState_Get(void);
bool GateState_CompareAndSet(GateState_t expected, GateState_t newState);

void LED_Set(uint32_t color_mask);
void LED_AllOff(void);

#endif
