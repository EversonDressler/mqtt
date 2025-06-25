#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "bsp/board.h"
#include <string.h>
#include "hardware/adc.h"  // <-- Necessário para ADC (temperatura)
#include "hardware/gpio.h"

// Configurações MQTT
const char* mqtt_server = "35.172.255.228";//"192.168.1.141";
const char* mqtt_topic = "upload";
const char* mqtt_temp_topic = "upload";  // <-- Novo tópico para temperatura
const char* mqtt_user = NULL;//"projeto";
const char* mqtt_pass = NULL;//"1234";

mqtt_client_t* mqtt_cliente = NULL;
bool mqtt_conectado = false;

// Informações do cliente MQTT
struct mqtt_connect_client_info_t info_cliente = {
    "Everson", 
    mqtt_user, 
    mqtt_pass, 
    0, 
    NULL, 
    NULL, 
    0, 
    0
};

// Callback para dados recebidos
static void mqtt_dados_recebidos_cb(void *arg, const u8_t *dados, u16_t comprimento, u8_t flags) {
    char mensagem[31];
    u16_t tamanho_util = (comprimento < sizeof(mensagem) - 1) ? comprimento : (sizeof(mensagem) - 1);
    memcpy(mensagem, dados, tamanho_util);
    mensagem[tamanho_util] = '\0';

    printf("Dados recebidos: %s\n", mensagem);

    if(strncmp(mensagem, "on", 2) == 0)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
    } 
    else if(strncmp(mensagem, "off", 3) == 0)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
    }
}

// Callback ao receber o nome do tópico
static void mqtt_chegando_publicacao_cb(void *arg, const char *topico, u32_t tamanho) {
    printf("Conteudo do topico como string: %s\n", topico);
}

// Callback de requisições MQTT (subscribe/publish)
static void mqtt_req_cb(void *arg, err_t erro) {
    printf("Requisição MQTT retorno: %d\n", erro);
}

// Callback de conexão MQTT
static void mqtt_conectado_cb(mqtt_client_t *cliente, void *arg, mqtt_connection_status_t status) {
    printf("Status de conexão MQTT: %d\n", status);

    if (status == MQTT_CONNECT_ACCEPTED) {
        mqtt_conectado = true;
        printf("Conexão MQTT aceita.\n");

        err_t erro_sub = mqtt_sub_unsub(cliente, mqtt_topic, 0, mqtt_req_cb, arg, 1);
        if (erro_sub == ERR_OK) {
            printf("Inscrição no tópico '%s' realizada com sucesso.\n", mqtt_topic);
        } else {
            printf("Falha ao se inscrever no tópico.\n");
        }
    } else {
        mqtt_conectado = false;
        printf("Conexão MQTT rejeitada com status: %d\n", status);
    }
}

// Callback de desconexão
static void mqtt_desconectado_cb(mqtt_client_t *cliente, void *arg) {
    mqtt_conectado = false;
    printf("MQTT desconectado.\n");
}

// Função para iniciar/conectar MQTT
void tentar_reconectar_mqtt() {
    if (mqtt_cliente == NULL) {
        mqtt_cliente = mqtt_client_new();
        mqtt_set_inpub_callback(mqtt_cliente, mqtt_chegando_publicacao_cb, mqtt_dados_recebidos_cb, NULL);
    }

    ip_addr_t endereco_broker;
    if (!ip4addr_aton(mqtt_server, &endereco_broker)) {
        printf("Endereço IP do broker inválido.\n");
        return;
    }

    err_t err = mqtt_client_connect(mqtt_cliente, &endereco_broker, 1883, mqtt_conectado_cb, NULL, &info_cliente);
    if (err != ERR_OK) {
        printf("Erro ao tentar conectar ao broker: %d\n", err);
    }
}

// Função para ler a temperatura interna
float ler_temperatura_celsius() {
    adc_select_input(4);  // Temperatura está no canal 4
    uint16_t leitura = adc_read();
    const float conversao = 3.3f / (1 << 12);  // 12 bits ADC
    float tensao = leitura * conversao;
    return 27.0f - (tensao - 0.706f) / 0.001721f;
}

int main() {
    stdio_init_all();

    if (cyw43_arch_init()) {
        printf("Falha na inicialização do Wi-Fi.\n");
        return -1;
    }

    cyw43_arch_enable_sta_mode();

    printf("Conectando ao Wi-Fi...\n");
    //if (cyw43_arch_wifi_connect_timeout_ms("studio", "dressdrum", CYW43_AUTH_WPA2_AES_PSK, 30000))
    if (cyw43_arch_wifi_connect_timeout_ms("Desktop_F8916987", "1623307530576397", CYW43_AUTH_WPA2_AES_PSK, 30000))
    {
        printf("Falha ao conectar ao Wi-Fi.\n");
        return 1;
    }

    printf("Conectado ao Wi-Fi.\n");
    uint8_t *ip = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
    printf("Endereço IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    // Inicializa o ADC para leitura da temperatura
    adc_init();
    adc_set_temp_sensor_enabled(true);

    tentar_reconectar_mqtt();

    bool enviado = false;
    absolute_time_t ultimo_tentativa = get_absolute_time();
    absolute_time_t ultimo_envio_temp = get_absolute_time();

    while (true) {
        cyw43_arch_poll();

        // Tentativa de reconexão MQTT a cada 5 segundos
        if (!mqtt_conectado && absolute_time_diff_us(ultimo_tentativa, get_absolute_time()) > 5e6) {
            printf("Tentando reconectar ao MQTT...\n");
            tentar_reconectar_mqtt();
            ultimo_tentativa = get_absolute_time();
        }

        // Envia temperatura a cada 1 segundo
        if (mqtt_conectado && absolute_time_diff_us(ultimo_envio_temp, get_absolute_time()) > 1e6) {
            float temp = ler_temperatura_celsius();
            char mensagem[32];
            snprintf(mensagem, sizeof(mensagem), "%.2f", temp);
            mqtt_publish(mqtt_cliente, mqtt_temp_topic, mensagem, strlen(mensagem), 0, 0, mqtt_req_cb, NULL);
            printf("Temperatura enviada: %s °C\n", mensagem);
            ultimo_envio_temp = get_absolute_time();
        }

        // Botão
        if (mqtt_conectado && board_button_read()) {
            if (!enviado) {
                mqtt_publish(mqtt_cliente, mqtt_topic, "pressionado", strlen("pressionado"), 0, 0, mqtt_req_cb, NULL);
                enviado = true;
            }
            while (board_button_read()) {
                cyw43_arch_poll();
                sleep_ms(10);
            }
        } else if (mqtt_conectado && enviado) {
            mqtt_publish(mqtt_cliente, mqtt_topic, "solto", strlen("solto"), 0, 0, mqtt_req_cb, NULL);
            enviado = false;
        }

        sleep_ms(100);
    }

    return 0;
}
