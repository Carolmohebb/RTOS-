#include "tm4c123gh6pm.h"
#include "main.h"
#include "basic_io.h"

static void GPIO_Init(void)
{
    /* Enable clocks for ports B, D, E, F and wait until ready */
    SYSCTL_RCGCGPIO_R |= RCGCGPIO_ALL;
    while ((SYSCTL_PRGPIO_R & RCGCGPIO_ALL) != RCGCGPIO_ALL) { }

    /* ---------- Port F: RGB outputs (PF1-3), obstacle (PF4),
                          manual mode hold button (PF0)          ---------- */
    /* PF0 is locked by default on TM4C123 - must unlock first */
    GPIO_PORTF_LOCK_R  =  0x4C4F434BU;
    GPIO_PORTF_CR_R   |=  BTN_PF0;

    GPIO_PORTF_AMSEL_R &= ~(BTN_PF0 | BTN_PF4 | LED_MASK);
    GPIO_PORTF_PCTL_R  &= ~0x000FFFFFU;
    GPIO_PORTF_AFSEL_R &= ~(BTN_PF0 | BTN_PF4 | LED_MASK);

    GPIO_PORTF_DIR_R   |=  LED_MASK;
    GPIO_PORTF_DIR_R   &= ~(BTN_PF0 | BTN_PF4);

    GPIO_PORTF_PUR_R   |=  BTN_PF0 | BTN_PF4;
    GPIO_PORTF_DEN_R   |=  BTN_PF0 | BTN_PF4 | LED_MASK;
    GPIO_PORTF_DATA_R  &= ~LED_MASK;

    /* ---------- Port E: PE0, PE1 buttons (pull-down, active-high) ---------- */
    GPIO_PORTE_AMSEL_R &= ~(BTN_PE0 | BTN_PE1);
    GPIO_PORTE_PCTL_R  &= ~0x000000FFU;
    GPIO_PORTE_AFSEL_R &= ~(BTN_PE0 | BTN_PE1);
    GPIO_PORTE_DIR_R   &= ~(BTN_PE0 | BTN_PE1);
    GPIO_PORTE_PDR_R   |=  (BTN_PE0 | BTN_PE1);
    GPIO_PORTE_DEN_R   |=  (BTN_PE0 | BTN_PE1);

    /* ---------- Port B: PB0, PB1 buttons (pull-down, active-high) ---------- */
    GPIO_PORTB_AMSEL_R &= ~(BTN_PB0 | BTN_PB1);
    GPIO_PORTB_PCTL_R  &= ~0x000000FFU;
    GPIO_PORTB_AFSEL_R &= ~(BTN_PB0 | BTN_PB1);
    GPIO_PORTB_DIR_R   &= ~(BTN_PB0 | BTN_PB1);
    GPIO_PORTB_PDR_R   |=  (BTN_PB0 | BTN_PB1);
    GPIO_PORTB_DEN_R   |=  (BTN_PB0 | BTN_PB1);

    /* ---------- Port D: PD0, PD1 buttons (pull-down, active-high) ---------- */
    GPIO_PORTD_AMSEL_R &= ~(BTN_PD0 | BTN_PD1);
    GPIO_PORTD_PCTL_R  &= ~0x000000FFU;
    GPIO_PORTD_AFSEL_R &= ~(BTN_PD0 | BTN_PD1);
    GPIO_PORTD_DIR_R   &= ~(BTN_PD0 | BTN_PD1);
    GPIO_PORTD_PDR_R   |=  (BTN_PD0 | BTN_PD1);
    GPIO_PORTD_DEN_R   |=  (BTN_PD0 | BTN_PD1);
}

inline uint32_t Btn_PF0(void) { return (GPIO_PORTF_DATA_R & BTN_PF0) == 0; }
inline uint32_t Btn_PF4(void) { return (GPIO_PORTF_DATA_R & BTN_PF4) == 0; }
inline uint32_t Btn_PE0(void) { return (GPIO_PORTE_DATA_R & BTN_PE0) != 0; }
inline uint32_t Btn_PE1(void) { return (GPIO_PORTE_DATA_R & BTN_PE1) != 0; }
inline uint32_t Btn_PB0(void) { return (GPIO_PORTB_DATA_R & BTN_PB0) != 0; }
inline uint32_t Btn_PB1(void) { return (GPIO_PORTB_DATA_R & BTN_PB1) != 0; }
inline uint32_t Btn_PD0(void) { return (GPIO_PORTD_DATA_R & BTN_PD0) != 0; }
inline uint32_t Btn_PD1(void) { return (GPIO_PORTD_DATA_R & BTN_PD1) != 0; }

void LED_Set(uint32_t color_mask)
{
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~LED_MASK) | (color_mask & LED_MASK);
}

void LED_AllOff(void)
{
    GPIO_PORTF_DATA_R &= ~LED_MASK;
}

void Delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    while (ms--) { for (i = 0; i < 4000; i++) { } }
}

void GateState_Set(GateState_t newState)
{
    xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
    gateState = newState;
    xSemaphoreGive(xGateStateMutex);
}

GateState_t GateState_Get(void)
{
    GateState_t state;
    xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
    state = gateState;
    xSemaphoreGive(xGateStateMutex);
    return state;
}

bool GateState_CompareAndSet(GateState_t expected, GateState_t newState)
{
    bool success = false;
    xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
    if (gateState == expected)
    {
        gateState = newState;
        success = true;
    }
    xSemaphoreGive(xGateStateMutex);
    return success;
}

/* -----------------------------------------------------------------------
 * Interrupt configuration helpers
 *
 * Single edge:
 *   IS_R  = 0  -> edge sensitive
 *   IBE_R = 0  -> single edge (controlled by IEV_R)
 *   IEV_R = 1  -> rising edge
 *   IEV_R = 0  -> falling edge
 * --------------------------------------------------------------------- */
static void ISR_ConfigRising(volatile uint32_t *IS_R,
                              volatile uint32_t *IBE_R,
                              volatile uint32_t *IEV_R,
                              volatile uint32_t *ICR_R,
                              volatile uint32_t *IM_R,
                              uint32_t mask)
{
    *IS_R  &= ~mask;   /* edge sensitive  */
    *IBE_R &= ~mask;   /* single edge     */
    *IEV_R |=  mask;   /* rising edge     */
    *ICR_R  =  mask;   /* clear stale     */
    *IM_R  |=  mask;   /* unmask          */
}

static void ISR_ConfigFalling(volatile uint32_t *IS_R,
                               volatile uint32_t *IBE_R,
                               volatile uint32_t *IEV_R,
                               volatile uint32_t *ICR_R,
                               volatile uint32_t *IM_R,
                               uint32_t mask)
{
    *IS_R  &= ~mask;   /* edge sensitive  */
    *IBE_R &= ~mask;   /* single edge     */
    *IEV_R &= ~mask;   /* falling edge    */
    *ICR_R  =  mask;   /* clear stale     */
    *IM_R  |=  mask;   /* unmask          */
}

void Interrupt_Init(void)
{
    /* ---- Port F ----
       PF4 (obstacle, pull-up)    -> falling edge (pressed = low)
       PF0 (manual hold, pull-up) -> rising edge  (released = high)   */
    ISR_ConfigFalling(&GPIO_PORTF_IS_R, &GPIO_PORTF_IBE_R, &GPIO_PORTF_IEV_R,
                      &GPIO_PORTF_ICR_R, &GPIO_PORTF_IM_R, BTN_PF4);
    ISR_ConfigRising (&GPIO_PORTF_IS_R, &GPIO_PORTF_IBE_R, &GPIO_PORTF_IEV_R,
                      &GPIO_PORTF_ICR_R, &GPIO_PORTF_IM_R, BTN_PF0);
    /* NVIC: Port F = IRQ30, priority 5 */
    NVIC_EN0_R  |=  (1U << 30);
    NVIC_PRI7_R  = (NVIC_PRI7_R & 0xFF00FFFFU) | (5U << 21);

    /* ---- Port B: PB0 (drv open), PB1 (drv close)
       Pull-down, rising edge only (pressed = high)                    */
    ISR_ConfigRising(&GPIO_PORTB_IS_R, &GPIO_PORTB_IBE_R, &GPIO_PORTB_IEV_R,
                     &GPIO_PORTB_ICR_R, &GPIO_PORTB_IM_R,
                     BTN_PB0 | BTN_PB1);
    /* NVIC: Port B = IRQ1, priority 5 */
    NVIC_EN0_R  |=  (1U << 1);
    NVIC_PRI0_R  = (NVIC_PRI0_R & 0xFFFFFF00U) | (5U << 5);

    /* ---- Port D: PD0 (sec open), PD1 (sec close)
       Pull-down, rising edge only (pressed = high)                    */
    ISR_ConfigRising(&GPIO_PORTD_IS_R, &GPIO_PORTD_IBE_R, &GPIO_PORTD_IEV_R,
                     &GPIO_PORTD_ICR_R, &GPIO_PORTD_IM_R,
                     BTN_PD0 | BTN_PD1);
    /* NVIC: Port D = IRQ3, priority 5 */
    NVIC_EN0_R  |=  (1U << 3);
    NVIC_PRI0_R  = (NVIC_PRI0_R & 0xFF00FFFFU) | (5U << 21);

    /* ---- Port E: PE0 (open limit), PE1 (closed limit)
       Pull-down, rising edge only (pressed = high)                    */
    ISR_ConfigRising(&GPIO_PORTE_IS_R, &GPIO_PORTE_IBE_R, &GPIO_PORTE_IEV_R,
                     &GPIO_PORTE_ICR_R, &GPIO_PORTE_IM_R,
                     BTN_PE0 | BTN_PE1);
    /* NVIC: Port E = IRQ4, priority 5 */
    NVIC_EN0_R  |=  (1U << 4);
    NVIC_PRI1_R  = (NVIC_PRI1_R & 0xFFFFFF00U) | (5U << 5);
}

/* -----------------------------------------------------------------------
 * Helper: queue a ButtonEvent from an ISR
 * --------------------------------------------------------------------- */
static inline void QueueEventFromISR(ButtonID_t  btn,
                                     PanelID_t   panel,
                                     PressType_t type,
                                     BaseType_t *pxWoken)
{
    if (xButtonQueue == NULL) return;
    ButtonEvent_t ev = { btn, panel, type };
    xQueueSendFromISR(xButtonQueue, &ev, pxWoken);
}

/* -----------------------------------------------------------------------
 * ISR: Port F
 *   PF4 = Obstacle     (falling edge: pressed = low)
 *   PF0 = Manual hold  (rising edge:  released = high)
 *
 * PF0 rising (SW2 unchecked): sends PRESS_RELEASED for the current
 * gate direction so vGateControlTask stops the gate at STOPPED_MIDWAY.
 * --------------------------------------------------------------------- */
void GPIOF_Handler(void)
{
    if (xButtonQueue == NULL)
    {
        GPIO_PORTF_ICR_R = 0xFF;
        return;
    }

    BaseType_t xWoken = pdFALSE;

    /* PF4 - Obstacle: fires on falling edge (pressed) */
    if (GPIO_PORTF_RIS_R & BTN_PF4)
    {
        GPIO_PORTF_ICR_R = BTN_PF4;
        xSemaphoreGiveFromISR(xObstacleSem, &xWoken);
    }

    /* PF0 - Manual hold: fires on rising edge (released) */
    if (GPIO_PORTF_RIS_R & BTN_PF0)
    {
        GPIO_PORTF_ICR_R = BTN_PF0;

        /* Send PRESS_RELEASED based on current gate direction */
        GateState_t state = gateState;
        if (state == OPENING)
            QueueEventFromISR(BTN_DRV_OPEN,  PANEL_DRIVER, PRESS_RELEASED, &xWoken);
        else if (state == CLOSING)
            QueueEventFromISR(BTN_DRV_CLOSE, PANEL_DRIVER, PRESS_RELEASED, &xWoken);
        /* Gate not moving: release has no effect */
    }

    portYIELD_FROM_ISR(xWoken);
}

/* -----------------------------------------------------------------------
 * ISR: Port B  ->  PB0 = Driver OPEN, PB1 = Driver CLOSE
 *   Rising edge only (pressed = high).
 *   Press type determined by PF0 state:
 *     PF0 low (SW2 held) = PRESS_MANUAL
 *     PF0 high           = PRESS_ONETOUCH
 *   No release handling here — release is handled by PF0 rising edge.
 * --------------------------------------------------------------------- */
void GPIOB_Handler(void)
{
    if (xButtonQueue == NULL)
    {
        GPIO_PORTB_ICR_R = 0xFF;
        return;
    }

    BaseType_t xWoken = pdFALSE;

    /* Determine press type from PF0 state */
    PressType_t pressType = ((GPIO_PORTF_DATA_R & BTN_PF0) == 0)
                            ? PRESS_MANUAL
                            : PRESS_ONETOUCH;

    /* PB0 - Driver OPEN (rising edge = pressed) */
    if (GPIO_PORTB_RIS_R & BTN_PB0)
    {
        GPIO_PORTB_ICR_R = BTN_PB0;
        QueueEventFromISR(BTN_DRV_OPEN, PANEL_DRIVER, pressType, &xWoken);
    }

    /* PB1 - Driver CLOSE (rising edge = pressed) */
    if (GPIO_PORTB_RIS_R & BTN_PB1)
    {
        GPIO_PORTB_ICR_R = BTN_PB1;
        QueueEventFromISR(BTN_DRV_CLOSE, PANEL_DRIVER, pressType, &xWoken);
    }

    portYIELD_FROM_ISR(xWoken);
}

/* -----------------------------------------------------------------------
 * ISR: Port D  ->  PD0 = Security OPEN, PD1 = Security CLOSE
 *   Rising edge only. Same PF0 manual mode logic as Port B.
 * --------------------------------------------------------------------- */
void GPIOD_Handler(void)
{
    if (xButtonQueue == NULL)
    {
        GPIO_PORTD_ICR_R = 0xFF;
        return;
    }

    BaseType_t xWoken = pdFALSE;

    PressType_t pressType = ((GPIO_PORTF_DATA_R & BTN_PF0) == 0)
                            ? PRESS_MANUAL
                            : PRESS_ONETOUCH;

    /* PD0 - Security OPEN */
    if (GPIO_PORTD_RIS_R & BTN_PD0)
    {
        GPIO_PORTD_ICR_R = BTN_PD0;
        QueueEventFromISR(BTN_SEC_OPEN, PANEL_SECURITY, pressType, &xWoken);
    }

    /* PD1 - Security CLOSE */
    if (GPIO_PORTD_RIS_R & BTN_PD1)
    {
        GPIO_PORTD_ICR_R = BTN_PD1;
        QueueEventFromISR(BTN_SEC_CLOSE, PANEL_SECURITY, pressType, &xWoken);
    }

    portYIELD_FROM_ISR(xWoken);
}

/* -----------------------------------------------------------------------
 * ISR: Port E  ->  PE0 = Open Limit, PE1 = Closed Limit
 *   Rising edge only (pressed = high).
 * --------------------------------------------------------------------- */
void GPIOE_Handler(void)
{
    if (xButtonQueue == NULL)
    {
        GPIO_PORTE_ICR_R = 0xFF;
        return;
    }

    BaseType_t xWoken = pdFALSE;

    /* PE0 - Open Limit */
    if (GPIO_PORTE_RIS_R & BTN_PE0)
    {
        GPIO_PORTE_ICR_R = BTN_PE0;
        xSemaphoreGiveFromISR(xOpenLimitSem, &xWoken);
    }

    /* PE1 - Closed Limit */
    if (GPIO_PORTE_RIS_R & BTN_PE1)
    {
        GPIO_PORTE_ICR_R = BTN_PE1;
        xSemaphoreGiveFromISR(xClosedLimitSem, &xWoken);
    }

    portYIELD_FROM_ISR(xWoken);
}

int main(void)
{
    GPIO_Init();

    /* Create Queue */
    xButtonQueue = xQueueCreate(10, sizeof(ButtonEvent_t));

    /* Create Semaphores */
    xOpenLimitSem   = xSemaphoreCreateBinary();
    xClosedLimitSem = xSemaphoreCreateBinary();
    xObstacleSem    = xSemaphoreCreateBinary();

    /* Create Mutex */
    xGateStateMutex = xSemaphoreCreateMutex();

    Interrupt_Init();

    /* Create Tasks */
    xTaskCreate(vInputTask,       "Input",  128, NULL, 3, NULL);
    xTaskCreate(vGateControlTask, "Gate",   128, NULL, 2, NULL);
    xTaskCreate(vLEDTask,         "LED",    128, NULL, 2, NULL);
    xTaskCreate(vSafetyTask,      "Safety", 128, NULL, 4, NULL);

    /* Start Scheduler */
    vTaskStartScheduler();

    while (1) {}
}