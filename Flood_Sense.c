/*
  Material de suporte
  https://www.raspberrypi.com/documentation/pico-sdk/networking.html#group_pico_cyw43_arch_1ga33cca1c95fc0d7512e7fef4a59fd7475
 */

#include "General.h"    // Biblioteca geral do sistema
#include "Led.h"        // Biblioteca geral do sistema
#include "Buzzer.h"     // Biblioteca do buzzer
#include "Button.h"     // Biblioteca do botão
#include "Led_Matrix.h" // Biblioteca para controle da matriz de LEDs
#include "ssd1306.h"    // Biblioteca para controle do display OLED

// Credenciais WIFI - Tome cuidado se publicar no github!
#define WIFI_SSID "TSUNAMI_EVERALDO" // Nome da rede Wi-Fi
#define WIFI_PASSWORD "amizade5560"  // Senha da rede Wi-Fi

#define ALERT_THRESHOLD_A 12
#define ALERT_THRESHOLD_B 7

#define ATTENTION_THRESHOLD_A 9
#define ATTENTION_THRESHOLD_B 5

#define INITIAL_LEVEL_A 7
#define INITIAL_LEVEL_B 4

volatile bool is_region_A = true;
bool buzzer_on = false;

volatile uint8_t current_level_A = INITIAL_LEVEL_A;
volatile uint8_t current_level_B = INITIAL_LEVEL_B;

#define MAX_READINGS 10

uint8_t readings_A[MAX_READINGS] = {INITIAL_LEVEL_A}; // Array para armazenar os níveis de água da região A
uint8_t readings_B[MAX_READINGS] = {INITIAL_LEVEL_B}; // Array para armazenar os níveis de água da região B

#define MAX_EVENTS 10
#define EVENT_LENGTH 64

char event_log[MAX_EVENTS][EVENT_LENGTH] = {'\0'}; // Array para armazenar os eventos
int total_events = 0;

char region[20];

static err_t tcp_server_accept(void *arg, struct tcp_pcb *newpcb, err_t err); // Função de callback ao aceitar conexões TCP

static err_t tcp_server_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err); // Função de callback para processar requisições HTTP

void user_request(char **request); // Tratamento do request do usuário

void add_reading(uint8_t new_value, uint8_t readings[]); // Move todos os elementos para a esquerda e adiciona novo valor no final

void convert_readings_to_JSON(uint8_t *readings, char *buffer, int size); // Converte os dados de leitura para JSON

void add_event(const char *new_event);

void configure_display(ssd1306_t *ssd); // Configuração do display OLED

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
                add_reading(current_level_A, readings_A);
            }
            else
            {
                current_level_B++;
                add_reading(current_level_B, readings_B);
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
                    add_reading(current_level_A, readings_A);
                }
                else
                    add_reading(current_level_A, readings_A);
            }
            else
            {
                if (current_level_B > 0)
                {
                    current_level_B--;
                    add_reading(current_level_B, readings_B);
                }
                else
                    add_reading(current_level_B, readings_B);
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

    ssd1306_t ssd; // Estrutura que representa o display OLED SSD1306

    configure_display(&ssd); // Configuração do display
    configure_leds();        // Configura os LEDs
    configure_buzzer();      // Configura o buzzer
    configure_leds_matrix(); // Configura a matriz de LEDs

    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);

    ssd1306_draw_string(&ssd, "Inicializando", 5, 5);
    ssd1306_send_data(&ssd);

    // Inicializa a arquitetura do cyw43
    while (cyw43_arch_init())
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Falha", 5, 5);
        ssd1306_send_data(&ssd);

        sleep_ms(100);
        return -1;
    }

    // GPIO do CI CYW43 em nível baixo
    cyw43_arch_gpio_put(LED_PIN, 0);

    // Ativa o Wi-Fi no modo Station, de modo a que possam ser feitas ligações a outros pontos de acesso Wi-Fi.
    cyw43_arch_enable_sta_mode();

    // Conectar à rede WiFI - fazer um loop até que esteja conectado

    ssd1306_fill(&ssd, false);
    ssd1306_draw_string(&ssd, "Conectando...", 5, 5);
    ssd1306_send_data(&ssd);

    while (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 20000))
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Falha", 5, 5);
        ssd1306_send_data(&ssd);

        sleep_ms(100);
        return -1;
    }

    // Configura o servidor TCP - cria novos PCBs TCP. É o primeiro passo para estabelecer uma conexão TCP.
    struct tcp_pcb *server = tcp_new();
    if (!server)
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Falha no server", 5, 5);
        ssd1306_send_data(&ssd);
        return -1;
    }

    // vincula um PCB (Protocol Control Block) TCP a um endereço IP e porta específicos.
    if (tcp_bind(server, IP_ADDR_ANY, 80) != ERR_OK)
    {
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, "Falha porta 80", 5, 5);
        ssd1306_send_data(&ssd);
        return -1;
    }

    // Coloca um PCB (Protocol Control Block) TCP em modo de escuta, permitindo que ele aceite conexões de entrada.
    server = tcp_listen(server);

    // Define uma função de callback para aceitar conexões TCP de entrada. É um passo importante na configuração de servidores TCP.
    tcp_accept(server, tcp_server_accept);

    // Inicializa o conversor ADC
    adc_init();
    adc_set_temp_sensor_enabled(true);

    while (true)
    {
        cyw43_arch_poll(); // Necessário para manter o Wi-Fi ativo
        sleep_ms(100);     // Reduz o uso da CPU

        if (is_region_A)
            update_matrix_from_level(current_level_A, ATTENTION_THRESHOLD_A, ALERT_THRESHOLD_A);
        else
            update_matrix_from_level(current_level_B, ATTENTION_THRESHOLD_B, ALERT_THRESHOLD_B);

        if (buzzer_on)
            beep_alert(); // Liga o buzzer
        else
            set_buzzer_level(BUZZER_A, 0); // Desliga o buzzer


        // Define a string com base na variável
        snprintf(region, sizeof(region), "Regiao: %s", is_region_A ? "A" : "B");

        // Exibe no display
        ssd1306_fill(&ssd, false);
        ssd1306_draw_string(&ssd, ipaddr_ntoa(&netif_default->ip_addr), 5, 5);
        ssd1306_draw_string(&ssd, "Porta 80", 5, 18);
        ssd1306_draw_string(&ssd, region, 5, 50);
        ssd1306_send_data(&ssd);
    }

    // Desligar a arquitetura CYW43.
    cyw43_arch_deinit();
    return 0;
}

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
        set_led_color(ORANGE); // Liga o LED laranja
        add_event("🟠 LED-Atenção | Ligado");
    }
    else if (strstr(request, "GET /?periferico=ledR&acao=ligar") != NULL)
    {
        set_led_color(RED); // Liga o LED vermelho
        add_event("🔴 LED-Alerta | Ligado");
    }
    else if (strstr(request, "GET /?periferico=ledG&acao=ligar") != NULL)
    {
        set_led_color(GREEN); // Liga o LED verde
        add_event("🟢 LED-Normal | Ligado");
    }
    else if (strstr(request, "GET /?periferico=ledO&acao=desligar") != NULL)
    {
        set_led_color(DARK); // Desliga todos os LEDs
        add_event("🟠 LED-Atenção | Desligado");
    }
    else if (strstr(request, "GET /?periferico=ledR&acao=desligar") != NULL)
    {
        set_led_color(DARK); // Desliga todos os LEDs
        add_event("🔴 LED-Alerta | Desligado");
    }
    else if (strstr(request, "GET /?periferico=ledG&acao=desligar") != NULL)
    {
        set_led_color(DARK); // Desliga todos os LEDs
        add_event("🟢 LED-Normal | Desligado");
    }
    // Adicione aqui para o BUZZER e outros LEDs/ações se necessário
    else if (strstr(request, "GET /?periferico=buzzer&acao=ligar") != NULL)
    {
        buzzer_on = true;
        add_event("🔊 Buzzer | Ligado");
    }
    else if (strstr(request, "GET /?periferico=buzzer&acao=desligar") != NULL)
    {
        buzzer_on = false; // Desliga o buzzer
        add_event("🔊 Buzzer | Desligado");
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

    // Tratamento de request - Controle dos LEDs
    user_request(&request);

    char class_region_A[16];
    char class_region_B[16];

    // Região A
    if (current_level_A >= ALERT_THRESHOLD_A)
    {
        strcpy(class_region_A, "Alerta");
    }
    else if (current_level_A >= ATTENTION_THRESHOLD_A)
    {
        strcpy(class_region_A, "Atenção");
    }
    else
    {
        strcpy(class_region_A, "Normal");
    }

    // Região B
    if (current_level_B >= ALERT_THRESHOLD_B)
    {
        strcpy(class_region_B, "Alerta");
    }
    else if (current_level_B >= ATTENTION_THRESHOLD_B)
    {
        strcpy(class_region_B, "Atenção");
    }
    else
    {
        strcpy(class_region_B, "Normal");
    }

    // Função para converter arrays uint8_t para string JSON, para JS usar nos gráficos:
    char readings_A_str[300] = {0};
    char readings_B_str[300] = {0};

    // Converta antes do snprintf principal
    convert_readings_to_JSON(readings_A, readings_A_str, sizeof(readings_A_str));
    convert_readings_to_JSON(readings_B, readings_B_str, sizeof(readings_B_str));

    char events_html[1024] = {'\0'};

    if (total_events > 0)
    {
        for (int i = total_events - 1; i >= 0; i--)
        {
            char line[128];
            snprintf(line, sizeof(line), "<tr><td>%s</td><tr>", event_log[i]);
            strncat(events_html, line, sizeof(events_html) - strlen(events_html) - 1);
        }
    }

    char html[6144];

    snprintf(html, sizeof(html),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Connection: close\r\n"
             "\r\n"
             "<!DOCTYPE html>"
             "<html>"
             "<head>"
             "<meta charset='UTF-8'>"
             "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
             "<title>FloodSense</title>"
             "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"
             "<style>"
             "body{font-family:sans-serif;background:#f0f0f0;padding:20px;text-align:center;}"
             ".b{display:block;margin:10px auto;padding:20px;border-radius:10px;box-shadow:0 0 5px #ccc;background:#fff;font-weight:bold;width:fit-content;max-width:100%;}"
             ".bk{display:inline-block;vertical-align:top;margin:10px;}"
             ".value{color:#1976d2;font-size:20px;}"
             ".status{padding:4px 8px;border-radius:4px;display:inline-block;}"
             ".Alerta{background:#e53935;color:#fff;}"
             ".Normal{background:#43a047;color:#fff;}"
             ".Atenção{background:#fb8c00;color:#fff;}"
             ".Blue{background:#1976d2;color:#fff;}"
             "button,input,select{padding:6px;margin:4px;border-radius:5px;border:1px solid #838282;font-size:14px;}"
             "table{margin:0 auto;border-collapse:collapse;}"
             "th,td{padding:4px 8px;border:1px solid #ccc;}"
             ".tab-btn{margin:10px;padding:10px;color:#fff;border:none;border-radius:5px;cursor:pointer;}"
             ".hidden{display:none;}"
             "</style>"
             "</head>"
             "<body>"
             "<h1>FloodSense Monitor</h1>"

             "<div>"
             "<button class='tab-btn Blue' onclick=\"showTab('monitor')\">👁️ Monitoramento</button>"
             "<button class='tab-btn Blue' onclick=\"showTab('controle')\">⚙️ Controle Manual</button>"
             "</div>"

             "<div id='monitor'>"

             "<div style='display: flex; flex-wrap: wrap; justify-content: center; gap:20px;'>"

             "<div>"
             "<div class='b bk'>"
             "<h2>Região A</h2>"
             "<p class='value'>Nível: %dm</p>"
             "<p class='status %s'>%s</p>"
             "</div>"

             "<div class='b bk'>"
             "<h2>Região B</h2>"
             "<p class='value'>Nível: %dm</p>"
             "<p class='status %s'>%s</p>"
             "</div>"

             "<div class='b bk'>"
             "<h2>Limiares</h2>"
             "<table>"
             "<tr><th>Região</th><th>Atenção (m)</th><th>Alerta (m)</th></tr>"
             "<tr><td>A</td><td>%d</td><td>%d</td></tr>"
             "<tr><td>B</td><td>%d</td><td>%d</td></tr>"
             "</table>"
             "</div>"

             "<div class='b'>"
             "<h2>Histórico de Níveis</h2>"
             "<div class='b bk'><canvas id='nivelChartA' width='300' height='200'></canvas></div>"
             "<div class='b bk'><canvas id='nivelChartB' width='300' height='200'></canvas></div>"
             "</div>"
             "</div>"
             "<div>"
             "<div class='b bk'>"
             "<h2>Últimos Eventos</h2>"
             "<table style='text-align:left;'>%s</table>"
             "</div>"
             "</div>"
             "</div>"
             "</div>"

             "<div id='controle' class='hidden'>"
             "<div class='b'>"
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
             "<button class='tab-btn Normal' type='submit' name='acao' value='ligar'>Ligar</button>"
             "<button class='tab-btn Alerta' type='submit' name='acao' value='desligar'>Desligar</button>"
             "</form>"
             "</div>"
             "</div>"

             "<script>"
             "function showTab(tabId){"
             "document.getElementById('monitor').classList.add('hidden');"
             "document.getElementById('controle').classList.add('hidden');"
             "document.getElementById(tabId).classList.remove('hidden');"
             "}"
             "new Chart(document.getElementById('nivelChartA').getContext('2d'),{type:'line',data:{labels:['1','2','3','4','5','6','7','8','9','10'],datasets:[{label:'Região A (m)',data:%s,borderColor:'#1976d2',backgroundColor:'rgba(25,118,210,0.2)',fill:true,tension:0.3}]},options:{scales:{x:{type:'linear',position:'bottom',min:1,max:%d},y:{beginAtZero:true}}}});"
             "new Chart(document.getElementById('nivelChartB').getContext('2d'),{type:'line',data:{labels:['1','2','3','4','5','6','7','8','9','10'],datasets:[{label:'Região B (m)',data:%s,borderColor:'#1976d2',backgroundColor:'rgba(25,118,210,0.2)',fill:true,tension:0.3}]},options:{scales:{x:{type:'linear',position:'bottom',min:1,max:%d},y:{beginAtZero:true}}}});"
             "</script>"
             
             "<script>(function(){setInterval(()=>{const controle=document.getElementById('controle');if(controle&&controle.classList.contains('hidden')){window.location.href='/';}},8000);})();</script>"
             "</body>"
             "</html>",

             // Variáveis para mostrar níveis atuais e classes:
             current_level_A, class_region_A, class_region_A,
             current_level_B, class_region_B, class_region_B,

             // Limiares:
             ATTENTION_THRESHOLD_A, ALERT_THRESHOLD_A,
             ATTENTION_THRESHOLD_B, ALERT_THRESHOLD_B,

             // Eventos:
             events_html,

             // dados dos arrays:
             readings_A_str, MAX_READINGS,
             readings_B_str, MAX_READINGS);

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

// Move todos os elementos para a esquerda e adiciona novo valor no final
void add_reading(uint8_t new_value, uint8_t readings[])
{
    for (int i = 0; i < MAX_READINGS - 1; i++)
    {
        readings[i] = readings[i + 1];
    }
    readings[MAX_READINGS - 1] = new_value;
}

// Converte os dados de leitura para JSON
void convert_readings_to_JSON(uint8_t *readings, char *buffer, int size)
{
    int len = 0;
    len += snprintf(buffer + len, size - len, "[");
    for (int i = 0; i < MAX_READINGS; i++)
    {
        len += snprintf(buffer + len, size - len, "{\"x\":%d,\"y\":%d}%s", i + 1, readings[i], (i < MAX_READINGS - 1) ? "," : "");
        if (len >= size)
            break;
    }
    snprintf(buffer + len, size - len, "]");
}

void add_event(const char *new_event)
{
    if (total_events < MAX_EVENTS)
    {
        total_events++;
    }
    else
    {
        for (int i = 1; i < MAX_EVENTS; i++)
        {
            strncpy(event_log[i - 1], event_log[i], EVENT_LENGTH);
        }
    }

    strncpy(event_log[total_events - 1], new_event, EVENT_LENGTH - 1);
    event_log[total_events - 1][EVENT_LENGTH - 1] = '\0';
}

// Função para configurar o display
void configure_display(ssd1306_t *ssd)
{
    i2c_init(I2C_PORT, 400 * 1000); // Inicializa o I2C à 400khz

    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C); // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA);                     // Pull up the data line
    gpio_pull_up(I2C_SCL);                     // Pull up the clock line

    ssd1306_init(ssd, WIDTH, HEIGHT, false, ADDRESS, I2C_PORT); // Inicializa o display
    ssd1306_config(ssd);                                        // Configura o display
    ssd1306_send_data(ssd);                                     // Envia os dados para o display
}