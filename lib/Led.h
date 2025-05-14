#ifndef LEDS_H
#define LEDS_H

#include "General.h"

// Definições dos pinos conectados aos LEDs
#define GREEN_LED 11 
#define BLUE_LED 12  
#define RED_LED 13  
#define WRAP_PWM_LED 5  // PWM para controle de brilho
#define LED_PIN CYW43_WL_GPIO_LED_PIN // GPIO do CI CYW43

// Estrutura para representar uma cor RGB
typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} led_color;

// Definições de cores para LEDs
extern const led_color GREEN;
extern const led_color ORANGE;
extern const led_color DARK;
extern const led_color RED;
extern const led_color BLUE;

// Inicializa os pinos dos LEDs
void configure_leds();

// Ajusta o brilho de um LED via PWM
void set_led_brightness(uint gpio, uint8_t level);

// Define a cor de saída RGB no LED RGB
void set_led_color(led_color color);

#endif