#include "Button.h"    // Header com definições relacionadas aos botões

// Função para configurar os botões com interrupções
void configure_button(uint8_t button)
{
    gpio_init(button); // Inicializa o GPIO do botão
    gpio_set_dir(button, GPIO_IN); // Configura como entrada
    gpio_pull_up(button); // Habilita pull-up interno
}