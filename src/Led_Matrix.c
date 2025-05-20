#include "Led_Matrix.h" // Inclusão da biblioteca para controlar a matriz de LEDs

refs pio;

// Matriz que será usada como base para colorir
volatile int8_t matrix[NUM_PIXELS] = {0};

void configure_leds_matrix()
{
    pio.ref = pio0;

    pio.state_machine = pio_claim_unused_sm(pio.ref, true);     // Obtém uma máquina de estado livre
    pio.offset = pio_add_program(pio.ref, &pio_matrix_program); // Adiciona o programa da matriz

    pio_matrix_program_init(pio.ref, pio.state_machine, pio.offset, LED_MATRIX); // Inicializa o programa
}

// Converte uma estrutura de cor RGB para um valor 32 bits conforme o protocolo da matriz
uint32_t rgb_matrix(led_color color)
{
    return (color.green << 24) | (color.red << 16) | (color.blue << 8);
}

void update_matrix_from_level(uint8_t current_level, uint8_t alert_threshold)
{
    // Limpa a matriz (todos os LEDs apagados)
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        matrix[i] = 0;
    }

    // Determina quantos níveis são necessários para acender uma linha
    int levels_per_line = (int)ceil((double)alert_threshold / 5);

    // Evita divisão por zero
    if (levels_per_line == 0)
        levels_per_line = 1;

    // Calcula o número de linhas a acender
    int lines_on = (int)floor((double)current_level * 5 / alert_threshold);
    
    if (lines_on > 5)
        lines_on = 5;

    // Acende as linhas correspondentes
    for (int line = 0; line < lines_on; line++)
    {
        int base = line * 5;
        for (int i = 0; i < 5; i++)
        {
            matrix[base + i] = 1;
        }
    }

    // Envia a matriz para os LEDs via PIO
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        uint32_t color = (matrix[i] == 1)
                             ? rgb_matrix(BLUE)
                             : rgb_matrix(DARK);

        pio_sm_put_blocking(pio.ref, pio.state_machine, color);
    }
}
