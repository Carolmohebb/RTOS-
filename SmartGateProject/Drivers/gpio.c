#include "gpio.h"

/* Simulated variables */
static int open_button = 0;
static int close_button = 0;
static int obstacle = 0;
static int open_limit = 0;
static int closed_limit = 0;

void GPIO_Init(void)
{
    /*Normally hardware init*/
}

/* ===== INPUTS ===== */

int Read_Open_Button(void)
{
    return open_button;
}

int Read_Close_Button(void)
{
    return close_button;
}

int Read_Obstacle(void)
{
    return obstacle;
}

int Read_Open_Limit(void)
{
		return open_limit;
}

int Read_Closed_Limit(void)
{
		return closed_limit;
}

/* ===== OUTPUTS ===== */

void Green_LED_On(void)
{
    /* simulate */
}

void Green_LED_Off(void)
{
    /* simulate */
}

void Red_LED_On(void)
{
    /* simulate */
}

void Red_LED_Off(void)
{
    /* simulate */
}

void Motor_Open(void)
{
		/* simulate */
}

void Motor_Close(void)
{
		/* simulate */
}

void Motor_Stop(void)
{
		/* simulate */
}