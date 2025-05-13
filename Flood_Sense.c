/*
  Material de suporte
  https://www.raspberrypi.com/documentation/pico-sdk/networking.html#group_pico_cyw43_arch_1ga33cca1c95fc0d7512e7fef4a59fd7475
 */

#include <stdio.h>  // Biblioteca padrão para entrada e saída
#include <string.h> // Biblioteca manipular strings
#include <stdlib.h> // funções para realizar várias operações, incluindo alocação de memória dinâmica (malloc)

#include "pico/stdlib.h"     // Biblioteca da Raspberry Pi Pico para funções padrão (GPIO, temporização, etc.)
#include "hardware/adc.h"    // Biblioteca da Raspberry Pi Pico para manipulação do conversor ADC
#include "pico/cyw43_arch.h" // Biblioteca para arquitetura Wi-Fi da Pico com CYW43

#include "lwip/pbuf.h"  // Lightweight IP stack - manipulação de buffers de pacotes de rede
#include "lwip/tcp.h"   // Lightweight IP stack - fornece funções e estruturas para trabalhar com o protocolo TCP
#include "lwip/netif.h" // Lightweight IP stack - fornece funções e estruturas para trabalhar com interfaces de rede (netif)

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "TSUNAMI_EVERALDO" // Nome da rede Wi-Fi
#define WIFI_PASSWORD "amizade5560"  // Senha da rede Wi-Fi

// Definição dos pinos dos LEDs
#define LED_PIN CYW43_WL_GPIO_LED_PIN // GPIO do CI CYW43
#define LED_BLUE_PIN 12               // GPIO12 - LED azul
#define LED_GREEN_PIN 11              // GPIO11 - LED verde
#define LED_RED_PIN 13                // GPIO13 - LED vermelho

volatile uint8_t alert_threshold_A = 80;
volatile uint8_t attention_threshold_A = 60;

volatile uint8_t alert_threshold_B = 75;
volatile uint8_t attention_threshold_B = 95;

volatile uint8_t current_level_A = 15;
volatile uint8_t current_level_B = 25;

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void);

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err);

// Função de callback para processar requisições HTTP
static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);

// Leitura da temperatura interna
float temp_read(void);

// Tratamento do request do usuário
void user_request(char **request);

// Trecho para modo BOOTSEL com botão B
#include "pico/bootrom.h"
#define botaoB 6
void gpio_irq_handler(uint gpio, uint32_t events)
{
    reset_usb_boot(0, 0);
}

int main()
{
    // Para ser utilizado o modo BOOTSEL com botão B
    gpio_init(botaoB);
    gpio_set_dir(botaoB, GPIO_IN);
    gpio_pull_up(botaoB);
    gpio_set_irq_enabled_with_callback(botaoB, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    // Inicializa todos os tipos de bibliotecas stdio padrão presentes que estão ligados ao binário.
    stdio_init_all();

    // Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
    gpio_led_bitdog();

    // Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        printf("Falha ao inicializar Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado
    printf("Conectando ao Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        printf("Falha ao conectar ao Wi-Fi\n");
        sleep_ms(100);
        return -1;
    }
    printf("Conectado ao Wi-Fi\n");

    // Caso seja a interface de rede padrão - imprimir o IP do dispositivo.
    if (netif_default)
    {
        printf("IP do dispositivo: %s\n", ipaddr_ntoa(&netif_default->ip_addr));
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        printf("Falha ao criar servidor TCP\n");
        return -1;
    }

    // vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        printf("Falha ao associar servidor TCP à porta 80\n");
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);
    printf("Servidor ouvindo na porta 80\n");

    // Inicializa o conversor ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true)
    {
        /*
         * Efetuar o processamento exigido pelo cyw43_driver ou pela stack TCP/IP.
         * Este método deve ser chamado periodicamente a partir do ciclo principal
         * quando se utiliza um estilo de sondagem pico_cyw43_arch
         */
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);     // Reduz o uso da CPU
    }

    // Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Funções ---------------------------------

// Inicializar os Pinos GPIO para acionamento dos LEDs da BitDogLab
void gpio_led_bitdog(void)
{
    // Configuração dos LEDs como saída
    gpio_init(LED_BLUE_PIN);
    gpio_set_dir(LED_BLUE_PIN, GPIO_OUT);
    gpio_put(LED_BLUE_PIN, false);

    gpio_init(LED_GREEN_PIN);
    gpio_set_dir(LED_GREEN_PIN, GPIO_OUT);
    gpio_put(LED_GREEN_PIN, false);

    gpio_init(LED_RED_PIN);
    gpio_set_dir(LED_RED_PIN, GPIO_OUT);
    gpio_put(LED_RED_PIN, false);
}

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário
void user_request(char **request)
{
    char *body = strstr(*request, "\r\n\r\n");
    if (body != NULL)
    {
        body += 4; // Pula os \r\n\r\n

        char regiao;
        int alerta, atencao;

        // Extrai os dados do corpo
        if (sscanf(body, "regiao=%c&limiteAlerta=%d&limiteAtencao=%d", &regiao, &alerta, &atencao) == 3)
        {
            if (regiao == 'A')
            {
                alert_threshold_A = alerta;
                attention_threshold_A = atencao;
            }
            else if (regiao == 'B')
            {
                alert_threshold_B = alerta;
                attention_threshold_B = atencao;
            }
        }
        else
        {
            printf("Erro ao interpretar o corpo do POST.\n");
        }
    }
};

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    if (!p)
    {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // Alocação do request na memória dinâmica
    char *request = (char *)malloc(p->len + 1);
    memcpy(request, p->payload, p->len);
    request[p->len] = '\0';

    printf("Request: %s\n", request);

    // Tratamento de request - Controle dos LEDs
    user_request(&request);

    // Cria a resposta HTML
    char html[2048];

    // Cria a resposta HTML em partes
    snprintf(html, sizeof(html),
             "<!DOCTYPE html><html>"
             "<head>"
             "<meta charset='UTF-8'>"
             "<meta name='viewport'content='width=device-width,initial-scale=1.0'>"
             "<title>FloodSense</title>"
             "<style>"
             "body{font-family:sans-serif;background:#f0f0f0;padding:20px;text-align:center;}"
             ".b{margin:10px auto;background:#fff;padding:30px;border-radius:10px;box-shadow:0 0 5px #ccc;font-weight:bold;max-width:300px;}"
             ".bk{display:inline-block;margin-left:10px;margin-right:10px;}"
             ".value{color:#1976d2;}"
             ".status{padding:4px 8px;border-radius:4px;display:inline-block;}"
             ".alerta{background:#e53935;color:#fff;}"
             ".normal{background:#43a047;color:#fff;}"
             ".atencao{background:#fb8c00;color:#fff;}"
             "button,input,select{padding:6px;margin:4px;border-radius:5px;border:1px solid #838282;font-size:14px;}"
             "table{margin:0 auto;border-collapse:collapse;}"
             "th,td{padding:4px 8px;border:1px solid #ccc;}"
             "</style>"
             "</head>"
             "<body>"
             "<h1>FloodSense Monitor</h1>"
             "<div class='b bk'><h2>Região A</h2><p class='value'>Nível: %dm</p><p class='status alerta'>Alerta</p></div>"
             "<div class='b bk'><h2>Região B</h2><p class='value'>Nível: %dm</p><p class='status normal'>Normal</p></div>"
             "<div class='b'>"
             "<h2>Limiares Salvos</h2>"
             "<table>"
             "<tr><th>Região</th><th>Alerta (m)</th><th>Atenção (m)</th></tr>"
             "<tr><td>A</td><td>%d</td><td>%d</td></tr>"
             "<tr><td>B</td><td>%d</td><td>%d</td></tr>"
             "</table>"
             "</div>"
             "<div class='b bk'>"
             "<h2>Alterar Limiares</h2>"
             "<form method='POST' action='/'>"
             "<select name='regiao'>"
             "<option value=''>Região</option>"
             "<option value='A'>A</option>"
             "<option value='B'>B</option>"
             "</select><br>"
             "<input type='number' name='limiteAlerta' placeholder='Alerta' min='0' max='100'><br>"
             "<input type='number' name='limiteAtencao' placeholder='Atenção' min='0' max='100'><br>"
             "<button type='submit'>Alterar</button>"
             "</form>"
             "</div>"
             "</body>"
             "</html>",
             current_level_A,
             current_level_B,
             alert_threshold_A,
             attention_threshold_A,
             alert_threshold_B,
             attention_threshold_B);

    // Escreve dados para envio (mas não os envia imediatamente).
    tcp_write(tpcb, html, strlen(html), TCP_WRITE_FLAG_COPY);

    // Envia a mensagem
    tcp_output(tpcb);

    // libera memória alocada dinamicamente
    free(request);

    // libera um buffer de pacote (pbuf) que foi alocado anteriormente
    pbuf_free(p);

    return ERR_OK;
}
