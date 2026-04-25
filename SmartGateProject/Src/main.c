#include "tm4c123gh6pm.h"
#include "main.h"
#include "gpio.h"

#define LED_RED      (1U << 1)
#define LED_BLUE     (1U << 2)
#define LED_GREEN    (1U << 3)
#define LED_MASK     (LED_RED | LED_BLUE | LED_GREEN)

#define BTN_PF4      (1U << 4)
#define BTN_PE0      (1U << 0)
#define BTN_PE1      (1U << 1)
#define BTN_PB0      (1U << 0)
#define BTN_PB1      (1U << 1)
#define BTN_PD0      (1U << 0)
#define BTN_PD1      (1U << 1)

#define RCGCGPIO_B   (1U << 1)
#define RCGCGPIO_D   (1U << 3)
#define RCGCGPIO_E   (1U << 4)
#define RCGCGPIO_F   (1U << 5)
#define RCGCGPIO_ALL (RCGCGPIO_B | RCGCGPIO_D | RCGCGPIO_E | RCGCGPIO_F)

static void GPIO_Init(void)
{
    /* Enable clocks for ports B, D, E, F and wait until ready */
    SYSCTL_RCGCGPIO_R |= RCGCGPIO_ALL;
    while ((SYSCTL_PRGPIO_R & RCGCGPIO_ALL) != RCGCGPIO_ALL) { }

    /* ---------- Port F: RGB outputs (PF1-3) and button PF4 ---------- */
    GPIO_PORTF_AMSEL_R &= ~(BTN_PF4 | LED_MASK);
    GPIO_PORTF_PCTL_R  &= ~0x000FFFF0U;   /* digital GPIO function for PF1..PF4 */
    GPIO_PORTF_AFSEL_R &= ~(BTN_PF4 | LED_MASK);

    GPIO_PORTF_DIR_R   |=  LED_MASK;      /* PF1-3 outputs */
    GPIO_PORTF_DIR_R   &= ~BTN_PF4;       /* PF4 input */

    GPIO_PORTF_PUR_R   |=  BTN_PF4;       /* enable pull-up on PF4 switch */
    GPIO_PORTF_DEN_R   |=  BTN_PF4 | LED_MASK;
    GPIO_PORTF_DATA_R  &= ~LED_MASK;      /* LEDs off */

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

static inline uint32_t Btn_PF4(void) { return (GPIO_PORTF_DATA_R & BTN_PF4) == 0; }
static inline uint32_t Btn_PE0(void) { return (GPIO_PORTE_DATA_R & BTN_PE0) != 0; }
static inline uint32_t Btn_PE1(void) { return (GPIO_PORTE_DATA_R & BTN_PE1) != 0; }
static inline uint32_t Btn_PB0(void) { return (GPIO_PORTB_DATA_R & BTN_PB0) != 0; }
static inline uint32_t Btn_PB1(void) { return (GPIO_PORTB_DATA_R & BTN_PB1) != 0; }
static inline uint32_t Btn_PD0(void) { return (GPIO_PORTD_DATA_R & BTN_PD0) != 0; }
static inline uint32_t Btn_PD1(void) { return (GPIO_PORTD_DATA_R & BTN_PD1) != 0; }

static void LED_Set(uint32_t color_mask)
{
    GPIO_PORTF_DATA_R = (GPIO_PORTF_DATA_R & ~LED_MASK) | (color_mask & LED_MASK);
}

static void Delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    while (ms--) { for (i = 0; i < 4000; i++) { } }
}

int main(void)
{
	
		static const uint32_t color_tbl[7] = {
        LED_RED,                            /* PF4 */
        LED_BLUE,                           /* PE0 */
        LED_GREEN,                          /* PE1 */
        LED_RED  | LED_BLUE,                /* PB0 */
        LED_RED  | LED_GREEN,               /* PB1 */
        LED_BLUE | LED_GREEN,               /* PD0 */
        LED_RED  | LED_BLUE | LED_GREEN,    /* PD1 */
    };
    uint32_t prev[7] = {0};
    uint32_t led_state = 0;
		
    GPIO_Init();
		LED_Set(led_state);

    /* Create Queue */
    xButtonQueue = xQueueCreate(10, sizeof(ButtonEvent_t));

    /* Create Semaphores */
    xOpenLimitSem   = xSemaphoreCreateBinary();
    xClosedLimitSem = xSemaphoreCreateBinary();
		xObstacleSem = xSemaphoreCreateBinary();

    /* Create Mutex */
    xGateStateMutex = xSemaphoreCreateMutex();

    /* Create Tasks */
    xTaskCreate(vInputTask, "Input", 128, NULL, 3, NULL);
    xTaskCreate(vGateControlTask, "Gate", 128, NULL, 2, NULL);
    xTaskCreate(vLEDTask, "LED", 128, NULL, 2, NULL);
    xTaskCreate(vSafetyTask, "Safety", 128, NULL, 4, NULL);

    /* Start Scheduler */
    vTaskStartScheduler();

    while(1){
				uint32_t cur[7];
        uint32_t i;

        cur[0] = Btn_PF4();
        cur[1] = Btn_PE0();
        cur[2] = Btn_PE1();
        cur[3] = Btn_PB0();
        cur[4] = Btn_PB1();
        cur[5] = Btn_PD0();
        cur[6] = Btn_PD1();

        for (i = 0; i < 7; i++)
        {
            /* Rising edge: button just got pressed -> toggle its color bits. */
            if (cur[i] && !prev[i])
            {
                led_state ^= color_tbl[i];
                LED_Set(led_state);
            }
            prev[i] = cur[i];
        }

        Delay_ms(20);   /* debounce */
		}
}

static void GateState_Set(GateState_t newState)
{
    xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
    gateState = newState;
    xSemaphoreGive(xGateStateMutex);
}

static GateState_t GateState_Get(void)
{
    GateState_t state;
    xSemaphoreTake(xGateStateMutex, portMAX_DELAY);
    state = gateState;
    xSemaphoreGive(xGateStateMutex);
    return state;
}

static bool GateState_CompareAndSet(GateState_t expected, GateState_t newState)
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