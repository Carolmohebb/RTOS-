#include "tm4c123gh6pm.h"
#include "gpio.h"

#define RED_LED (1U << 1)
#define GREEN_LED (1U << 3)
#define BTN_DRV_OPEN (1<<4)
#define BTN_DRV_CLOSE (1<<0)

/* Simulated variables */
static int open_button = 0;
static int close_button = 0;
static int obstacle_button = 0;
static int open_limit_button = 0;
static int closed_limit_button = 0;
static int sec_open_button = 0;
static int sec_close_button = 0;

void GPIO_Init(void)
{
		SYSCTL_RCGCGPIO_R |= 0X20;
		while ((SYSCTL_PRGPIO_R & 0x20) == 0);
    GPIO_PORTF_LOCK_R = 0x4C4F434B;
    GPIO_PORTF_CR_R |= 0x01;
	
		GPIO_PORTF_DIR_R |= (GREEN_LED | RED_LED);
		GPIO_PORTF_DIR_R &= ~(BTN_DRV_OPEN | BTN_DRV_CLOSE);
    GPIO_PORTF_DEN_R |= (GREEN_LED | RED_LED | BTN_DRV_OPEN | BTN_DRV_CLOSE);
		GPIO_PORTF_PUR_R |= (BTN_DRV_OPEN | BTN_DRV_CLOSE);
	
		NVIC_EN0_R |= (1 << 30);
    NVIC_PRI7_R = (NVIC_PRI7_R & 0xFF00FFFF) | (5 << 21);
}

/* ===== INPUTS ===== */

int Read_Open_Button(void)
{
    return (GPIO_PORTF_DATA_R & BTN_DRV_OPEN) == 0;
}

int Read_Close_Button(void)
{
    return (GPIO_PORTF_DATA_R & BTN_DRV_CLOSE) == 0;
}

int Read_Obstacle_Button(void)
{
    return obstacle_button;
}

int Read_Open_Limit_Button(void)
{
		return open_limit_button;
}

int Read_Closed_Limit_Button(void)
{
		return closed_limit_button;
}

int Read_Sec_Open_Button(void)
{
    return sec_open_button;
}

int Read_Sec_Close_Button(void)
{
    return sec_close_button;
}

/* ===== OUTPUTS ===== */

void Green_LED_On(void)
{
    GPIO_PORTF_DATA_R |= GREEN_LED;
}

void Green_LED_Off(void)
{
    GPIO_PORTF_DATA_R &= ~GREEN_LED;
}

void Red_LED_On(void)
{
    GPIO_PORTF_DATA_R |= RED_LED;
}

void Red_LED_Off(void)
{
    GPIO_PORTF_DATA_R &= ~RED_LED;
}

void Motor_Open(void)
{
		Green_LED_On();
    Red_LED_Off();
}

void Motor_Close(void)
{
		Red_LED_On();
    Green_LED_Off();
}

void Motor_Stop(void)
{
		Green_LED_Off();
    Red_LED_Off();
}