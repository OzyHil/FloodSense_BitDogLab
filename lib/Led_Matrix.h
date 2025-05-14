#ifndef LED_MATRIX_H
#define LED_MATRIX_H

#include "General.h" // Programa específico para controle da matriz de LEDs
#include "Led.h" 

#define NUM_PIXELS 25 // Total de LEDs na matriz (5x5)
#define LED_MATRIX 7 // GPIO para controle da matriz de LEDs

// Estrutura para armazenar referências do PIO
typedef struct
{
    PIO ref;              // Referência ao PIO (pio0 ou pio1)
    uint offset;          // Offset do programa carregado
    uint state_machine;   // Máquina de estado usada
} refs;

void configure_leds_matrix(); // Função para configurar a matriz de LEDs

// Converte uma estrutura de cor RGB para um valor 32 bits
uint32_t rgb_matrix(led_color color);

// Função para desenhar as cores do semáforo
void update_matrix_from_level(uint8_t current_level, uint8_t attention_threshold, uint8_t alert_threshold);

void init_digit_colors(); // Inicializa as cores dos dígitos

#endif