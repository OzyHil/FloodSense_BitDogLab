#ifndef BUTTON_H
#define BUTTON_H

// Inclusão da biblioteca geral do sistema
#include "General.h"

// Definição dos pinos dos botões
#define BUTTON_A 5 // Botão A   
#define BUTTON_B 6  // Botão B
#define BUTTON_J 22  // Botão Joystick
#define DEBOUNCE_DELAY 300000 // Tempo de debounce em microssegundos

// Função para configurar os botões
void configure_button(uint8_t button);

#endif