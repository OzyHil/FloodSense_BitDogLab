#include "Led.h" // Header para controle de LEDs via PWM

// Definições de cores utilizando níveis PWM para cada canal RGB
const led_color DARK = {.red = 0, .green = 0, .blue = 0};       // Cor "apagado"
const led_color GREEN = {.red = 0, .green = 72, .blue = 0};     // Verde
const led_color ORANGE = {.red = 178, .green = 2, .blue = 0};  // Amarelo
const led_color RED = {.red = 178, .green = 0, .blue = 0};      // Vermelho
const led_color BLUE = {.red = 0, .green = 2, .blue = 52};      // Vermelho

// Inicializa os pinos dos LEDs RGB como PWM
void configure_leds()
{
    const uint leds[] = {GREEN_LED, BLUE_LED, RED_LED};

    for (int i = 0; i < 3; i++)
    {
        init_pwm(leds[i], WRAP_PWM_LED); // Configura PWM com o "wrap" definido para LEDs
    }
}

// Define o brilho de um LED individual baseado em PWM
void set_led_brightness(uint gpio, uint8_t level)
{
    uint slice_num = pwm_gpio_to_slice_num(gpio);                    // Obtém slice PWM
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(gpio), level); // Define nível do canal
}

// Define uma cor no LED RGB usando struct `led_color`
void set_led_color(led_color color)
{
    set_led_brightness(RED_LED, color.red);
    set_led_brightness(GREEN_LED, color.green);
    set_led_brightness(BLUE_LED, color.blue);
}