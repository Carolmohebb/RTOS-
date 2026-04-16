#ifndef GPIO_DRIVER_H
#define GPIO_DRIVER_H

#include <stdint.h>

/* LED colors */
#define GREEN   0
#define RED     1

/* LED states */
#define ON      1
#define OFF     0

/* Function prototypes */
void gpio_init(void);
void led_set(uint8_t color, uint8_t state);
uint8_t button_read(uint8_t btn_id);

#endif /* GPIO_DRIVER_H */