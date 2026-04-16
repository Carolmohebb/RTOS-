#include "gpio_driver.h"
#include "TM4C123GH6PM.h"

void gpio_init(void) {
    /* Enable clocks for Port E (buttons) and Port F (LEDs) */
    SYSCTL->RCGCGPIO |= (1<<4) | (1<<5);  /* Port E = bit4, Port F = bit5 */
    volatile int delay = SYSCTL->RCGCGPIO; /* small delay for clock to stabilize */

    /* Port F: PF1=Red LED, PF3=Green LED → set as OUTPUT */
    GPIOF->DIR  |=  (1<<1) | (1<<3);
    GPIOF->DEN  |=  (1<<1) | (1<<3);

    /* Port E: PE0-PE3 = 4 buttons → set as INPUT with pull-up */
    GPIOE->DIR  &= ~(0x0F);
    GPIOE->DEN  |=  (0x0F);
    GPIOE->PUR  |=  (0x0F);

    /* Port D: PD0-PD2 = limit + obstacle buttons → INPUT with pull-up */
    SYSCTL->RCGCGPIO |= (1<<3);  /* Port D = bit3 */
    GPIOD->DIR  &= ~(0x07);
    GPIOD->DEN  |=  (0x07);
    GPIOD->PUR  |=  (0x07);
}

void led_set(uint8_t color, uint8_t state) {
    if (color == GREEN) {
        if (state == ON)  GPIOF->DATA |=  (1<<3);
        else              GPIOF->DATA &= ~(1<<3);
    } else {
        if (state == ON)  GPIOF->DATA |=  (1<<1);
        else              GPIOF->DATA &= ~(1<<1);
    }
}

uint8_t button_read(uint8_t btn_id) {
    switch(btn_id) {
        case 0: return !(GPIOE->DATA & (1<<0)); /* Driver OPEN   */
        case 1: return !(GPIOE->DATA & (1<<1)); /* Driver CLOSE  */
        case 2: return !(GPIOE->DATA & (1<<2)); /* Security OPEN */
        case 3: return !(GPIOE->DATA & (1<<3)); /* Security CLOSE*/
        case 4: return !(GPIOD->DATA & (1<<0)); /* Open Limit    */
        case 5: return !(GPIOD->DATA & (1<<1)); /* Closed Limit  */
        case 6: return !(GPIOD->DATA & (1<<2)); /* Obstacle      */
        default: return 0;
    }
}