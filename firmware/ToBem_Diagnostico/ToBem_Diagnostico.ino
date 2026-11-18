/*
  Tô Bem — DIAGNÓSTICO (testa uma peça por vez, sem Wi-Fi)
  Abra o Monitor Serial em 115200 e copie o que aparecer.
*/
#include <Adafruit_NeoPixel.h>
#include "DHTesp.h"
#include "esp_system.h"

#define PIN_LED_PLACA 2    // LED azul da própria placa ESP32
#define PIN_ANEL1     33   // DIN do anel 1
#define PIN_ANEL2     25   // DIN do anel 2
int PIN_RADAR_RX = 21;    // TX do radar (o teste abaixo descobre o certo)
int PIN_RADAR_TX = 19;    // RX do radar
#define PIN_RADAR_OUT 18   // OUT/OT2 do radar
#define PIN_DHT       32
long RADAR_BAUD = 256000;  // LD2410C = 256000 | LD2420 = 115200

Adafruit_NeoPixel anel1(16, PIN_ANEL1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel anel2(16, PIN_ANEL2, NEO_GRB + NEO_KHZ800);
DHTesp dht;

const char* motivoReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "ligou normalmente (energia)";
    case ESP_RST_SW:       return "reinicio por software";
    case ESP_RST_PANIC:    return "TRAVOU (erro no codigo)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "TRAVOU (watchdog)";
    case ESP_RST_BROWNOUT: return "FALTA DE ENERGIA (brownout)";
    default:               return "outro";
  }
}

// Testa as duas posições de TX/RX e várias velocidades até o radar "falar"
void procurarRadar() {
  const int pinos[2][2] = {{21, 19}, {19, 21}};          // {RX do ESP32, TX do ESP32}
  const long velocidades[] = {256000, 115200, 57600, 9600};
  Serial.println("Procurando o radar (TX/RX e velocidade)...");
  for (int p = 0; p < 2; p++) {
    for (long v : velocidades) {
      Serial2.end(); delay(50);
      Serial2.begin(v, SERIAL_8N1, pinos[p][0], pinos[p][1]);
      delay(50);
      while (Serial2.available()) Serial2.read();          // descarta lixo
      delay(700);
      int bytes = Serial2.available();
      bool frameLD2410 = false; uint8_t a = 0, b;
      while (Serial2.available()) { b = Serial2.read(); if (a == 0xF4 && b == 0xF3) frameLD2410 = true; a = b; }
      Serial.printf("  RX=D%d TX=D%d  %6ld baud -> %d bytes%s\n", pinos[p][0], pinos[p][1], v, bytes,
                    frameLD2410 ? "  (formato LD2410 OK!)" : "");
      if (frameLD2410 || (bytes > 20 && v != 9600)) {
        PIN_RADAR_RX = pinos[p][0]; PIN_RADAR_TX = pinos[p][1]; RADAR_BAUD = v;
        Serial.printf(">>> RADAR ENCONTRADO: RX do ESP32 = D%d, TX do ESP32 = D%d, %ld baud\n", PIN_RADAR_RX, PIN_RADAR_TX, RADAR_BAUD);
        return;
      }
    }
  }
  Serial.println(">>> Radar NAO respondeu em nenhuma combinacao. Confira o fio do TX do radar e o GND.");
  Serial2.end(); Serial2.begin(256000, SERIAL_8N1, 21, 19);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n===== DIAGNOSTICO TO BEM =====");
  Serial.printf("Motivo do ultimo reinicio: %s\n", motivoReset());

  pinMode(PIN_LED_PLACA, OUTPUT);
  pinMode(PIN_RADAR_OUT, INPUT);

  // LEDs: brilho bem baixo e so 4 acesos, para gastar pouca energia
  anel1.begin(); anel1.setBrightness(15); anel1.clear(); anel1.show();
  anel2.begin(); anel2.setBrightness(15); anel2.clear(); anel2.show();

  dht.setup(PIN_DHT, DHTesp::DHT22);
  procurarRadar();
  Serial.println("Setup concluido. Testando a cada 2 segundos...");
}

void loop() {
  static int passo = 0;
  passo++;

  // 1) placa viva: LED azul da placa pisca
  digitalWrite(PIN_LED_PLACA, passo % 2);

  // 2) anéis: alterna entre o anel 1 (vermelho) e o anel 2 (verde)
  anel1.clear(); anel2.clear();
  if (passo % 2) { for (int i = 0; i < 4; i++) anel1.setPixelColor(i, anel1.Color(255, 0, 0)); }
  else           { for (int i = 0; i < 4; i++) anel2.setPixelColor(i, anel2.Color(0, 255, 0)); }
  anel1.show(); anel2.show();
  Serial.printf("[LED]   agora deveria acender: %s\n", passo % 2 ? "ANEL 1 em VERMELHO (pino 33)" : "ANEL 2 em VERDE (pino 25)");

  // 3) radar: conta quantos bytes chegaram e mostra o pino de presença
  int bytes = 0; uint8_t primeiros[8]; int n = 0;
  while (Serial2.available()) { uint8_t b = Serial2.read(); if (n < 8) primeiros[n++] = b; bytes++; }
  Serial.printf("[RADAR] bytes recebidos: %d | pino OUT (18) = %d", bytes, digitalRead(PIN_RADAR_OUT));
  if (n) { Serial.print(" | inicio:"); for (int i = 0; i < n; i++) Serial.printf(" %02X", primeiros[i]); }
  Serial.println(bytes ? "  -> radar CONVERSANDO" : "  -> radar MUDO (confira TX/RX (D21/D19) e o 5V)");

  // 4) temperatura
  TempAndHumidity th = dht.getTempAndHumidity();
  if (isnan(th.temperature)) Serial.println("[DHT22] sem leitura (confira o fio no pino 32)");
  else Serial.printf("[DHT22] %.1f C, %.0f%% umidade\n", th.temperature, th.humidity);

  Serial.println("-----");
  delay(2000);
}
