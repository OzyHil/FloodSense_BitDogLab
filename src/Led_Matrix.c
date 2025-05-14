#include "Led_Matrix.h" // Inclusão da biblioteca para controlar a matriz de LEDs

led_color DIGIT_COLORS[2]; // Cores dos dígitos

refs pio;

// Matriz que será usada como base para colorir
volatile int8_t matrix[NUM_PIXELS] = {0};

void init_digit_colors()
{
    DIGIT_COLORS[0] = DARK;
    DIGIT_COLORS[1] = BLUE;
}

void configure_leds_matrix()
{
    pio.ref = pio0;

    pio.state_machine = pio_claim_unused_sm(pio.ref, true);     // Obtém uma máquina de estado livre
    pio.offset = pio_add_program(pio.ref, &pio_matrix_program); // Adiciona o programa da matriz

    pio_matrix_program_init(pio.ref, pio.state_machine, pio.offset, LED_MATRIX); // Inicializa o programa

    init_digit_colors(); // Inicializa as cores dos dígitos
}

// Converte uma estrutura de cor RGB para um valor 32 bits conforme o protocolo da matriz
uint32_t rgb_matrix(led_color color)
{
    return (color.green << 24) | (color.red << 16) | (color.blue << 8);
}

void update_matrix_from_level(uint8_t current_level, uint8_t attention_threshold, uint8_t alert_threshold)
{
    // Limpa a matriz (todos os LEDs apagados)
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        matrix[i] = 0;
    }

    // Caso 1: abaixo do nível de atenção → tudo apagado
    if (current_level < attention_threshold)
    {
        // Nada a fazer, a matriz já está zerada
    }
    // Caso 2: nível igual ou superior ao de alerta → tudo aceso
    else if (current_level >= alert_threshold)
    {
        for (int i = 0; i < NUM_PIXELS; i++)
        {
            matrix[i] = 1;
        }
    }
    // Caso 3: intermediário → acende proporcionalmente de 1 a 4 fileiras
    else
    {
        // Calcular o número de fileiras acesas de forma gradual
        int range = alert_threshold - attention_threshold; // A faixa total
        int level_in_range = current_level - attention_threshold; // Quantos passos acima do limite de atenção
        int lines_on = (level_in_range * 5) / range; // Número de fileiras a serem acesas

        // Acende as linhas correspondentes de forma gradual
        for (int line = 0; line < lines_on; line++)
        {
            int base = line * 5;
            for (int i = 0; i < 5; i++)
            {
                matrix[base + i] = 1;
            }
        }
    }

    // Envia a matriz para os LEDs via PIO (1 = azul, 0 = apagado)
    for (int i = 0; i < NUM_PIXELS; i++)
    {
        uint32_t color = (matrix[i] == 1)
            ? rgb_matrix(DIGIT_COLORS[1]) // Azul
            : rgb_matrix(DIGIT_COLORS[0]); // Apagado

        pio_sm_put_blocking(pio.ref, pio.state_machine, color);
    }
}
