#ifndef GPIO_H
#define GPIO_H

void GPIO_Init(void);

/* Inputs */
int Read_Open_Button(void);
int Read_Close_Button(void);
int Read_Obstacle(void);
int Read_Open_Limit(void);
int Read_Closed_Limit(void);

/* Outputs */
void Green_LED_On(void);
void Green_LED_Off(void);
void Red_LED_On(void);
void Red_LED_Off(void);
void Motor_Open(void);
void Motor_Close(void);
void Motor_Stop(void);

#endif
