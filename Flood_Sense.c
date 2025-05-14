/*
  Material de suporte
  https://www.raspberrypi.com/documentation/pico-sdk/networking.html#group_pico_cyw43_arch_1ga33cca1c95fc0d7512e7fef4a59fd7475
 */

#include "General.h"    // Biblioteca geral do sistema
#include "Led.h"        // Biblioteca geral do sistema
#include "Buzzer.h"     // Biblioteca do buzzer
#include "Button.h"     // Biblioteca do botão
#include "Led_Matrix.h" // Biblioteca para controle da matriz de LEDs

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "" // Nome da rede Wi-Fi
#define WIFI_PASSWORD ""  // Senha da rede Wi-Fi

volatile bool is_region_A = true;

volatile uint8_t alert_threshold_A = 12;
volatile uint8_t attention_threshold_A = 9;
volatile uint8_t current_level_A = 7;

volatile uint8_t alert_threshold_B = 7;
volatile uint8_t attention_threshold_B = 5;
volatile uint8_t current_level_B = 4;

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

uint32_t last_time_button_J = 0; // Tempo do último pressionamento
uint32_t last_time_button_A = 0; // Tempo do último pressionamento
uint32_t last_time_button_B = 0; // Tempo do último pressionamento

void gpio_irq_handler(uint gpio, uint32_t events)
{
    uint32_t now = get_absolute_time();

    if (gpio == BUTTON_J)
    {
        if ((now - last_time_button_J) >= DEBOUNCE_DELAY)
        {
            is_region_A = !is_region_A;
            printf("Região ativa: %s\n", is_region_A ? "A" : "B");
            last_time_button_J = now;
        }
    }

    else if (gpio == BUTTON_A)
    {
        if ((now - last_time_button_A) >= DEBOUNCE_DELAY)
        {
            if (is_region_A)
            {
                current_level_A++;
                printf("Nível A: %d\n", current_level_A);
            }
            else
            {
                current_level_B++;
                printf("Nível B: %d\n", current_level_B);
            }
            last_time_button_A = now;
        }
    }

    else if (gpio == BUTTON_B)
    {
        if ((now - last_time_button_B) >= DEBOUNCE_DELAY)
        {
            if (is_region_A)
            {
                if (current_level_A > 0)
                {
                    current_level_A--;
                }
                printf("Nível A: %d\n", current_level_A);
            }
            else
            {
                if (current_level_B > 0)
                {
                    current_level_B--;
                }
                printf("Nível B: %d\n", current_level_B);
            }
            last_time_button_B = now;
        }
    }
}

int main()
{
    configure_button(BUTTON_J); // Configura o botão J
    configure_button(BUTTON_A); // Configura o botão A
    configure_button(BUTTON_B); // Configura o botão B
    gpio_set_irq_enabled_with_callback(BUTTON_J, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BUTTON_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BUTTON_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    init_system_config(); // Inicializa a configuração do sistema

    configure_leds();        // Configura os LEDs
    configure_buzzer();      // Configura o buzzer
    configure_leds_matrix(); // Configura a matriz de LEDs

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

        if (is_region_A)
        {
            update_matrix_from_level(current_level_A, attention_threshold_A, alert_threshold_A);
        }
        else
        {
            update_matrix_from_level(current_level_B, attention_threshold_B, alert_threshold_B);
        }
    }

    // Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

// -------------------------------------- Funções ---------------------------------

// Função de callback ao aceitar conexões TCP
static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    tcp_recv(newpcb, tcp_server_recv);
    return ERR_OK;
}

// Tratamento do request do usuário
void user_request(char **request_ptr)
{
    char *request = *request_ptr;

    if (strstr(request, "GET /?periferico=ledO&acao=ligar") != NULL)
    {
        // Lógica para LED Laranja LIGAR
        set_led_color(ORANGE); // Liga o LED laranja
    }
    else if (strstr(request, "GET /?periferico=ledR&acao=ligar") != NULL)
    {
        // Lógica para LED Vermelho LIGAR (exclusiva)
        set_led_color(RED); // Liga o LED vermelho
    }
    else if (strstr(request, "GET /?periferico=ledG&acao=ligar") != NULL)
    {
        // Lógica para LED Verde LIGAR (exclusiva)
        set_led_color(GREEN); // Liga o LED verde
    }
    else if (strstr(request, "GET /?periferico=ledO&acao=desligar") ||
             strstr(request, "GET /?periferico=ledR&acao=desligar") ||
             strstr(request, "GET /?periferico=ledG&acao=desligar"))
    {
        // Lógica para LED Laranja DESLIGAR
        set_led_color(DARK); // Desliga todos os LEDs
    }
    // Adicione aqui para o BUZZER e outros LEDs/ações se necessário
    else if (strstr(request, "GET /?periferico=buzzer&acao=ligar") != NULL)
    {
        beep_alert(); // Liga o buzzer
    }
    else if (strstr(request, "GET /?periferico=buzzer&acao=desligar") != NULL)
    {
        set_buzzer_level(BUZZER_A, 0); // Desliga o buzzer
    }
}

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

    char class_region_A[16];
    char class_region_B[16];

    // Região A
    if (current_level_A >= alert_threshold_A)
    {
        strcpy(class_region_A, "alerta");
    }
    else if (current_level_A >= attention_threshold_A)
    {
        strcpy(class_region_A, "atencao");
    }
    else
    {
        strcpy(class_region_A, "normal");
    }

    // Região B
    if (current_level_B >= alert_threshold_B)
    {
        strcpy(class_region_B, "alerta");
    }
    else if (current_level_B >= attention_threshold_B)
    {
        strcpy(class_region_B, "atencao");
    }
    else
    {
        strcpy(class_region_B, "normal");
    }

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

             "<div class='b bk'><h2>Região A</h2><p class='value'>Nível: %dm</p><p class='status %s'>Alerta</p></div>"
             "<div class='b bk'><h2>Região B</h2><p class='value'>Nível: %dm</p><p class='status %s'>Normal</p></div>"

             "<div class='b'>"
             "<h2>Limiares</h2>"
             "<table>"
             "<tr><th>Região</th><th>Atenção (m)</th><th>Alerta (m)</th></tr>"
             "<tr><td>A</td><td>%d</td><td>%d</td></tr>"
             "<tr><td>B</td><td>%d</td><td>%d</td></tr>"
             "</table>"
             "</div>"

             "<div class='b bk'>"
             "<h2>Controle Manual</h2>"
             "<form method='GET' action='/'>"
             "<label for='periferico'>Selecione:</label><br>"
             "<select name='periferico'>"
             "<option value=''>---</option>"
             "<option value='buzzer'>Buzzer</option>"
             "<option value='ledG'>LED - Normal</option>"
             "<option value='ledO'>LED - Atenção</option>"
             "<option value='ledR'>LED - Alerta</option>"
             "</select><br><br>"
             "<button type='submit' name='acao' value='ligar'>Ligar</button>"
             "<button type='submit' name='acao' value='desligar'>Desligar</button>"
             "</form>"
             "</div>"

             "</body>"
             "</html>",
             current_level_A,
             class_region_A,
             current_level_B,
             class_region_B,
             attention_threshold_A,
             alert_threshold_A,
             attention_threshold_B,
             alert_threshold_B);

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
