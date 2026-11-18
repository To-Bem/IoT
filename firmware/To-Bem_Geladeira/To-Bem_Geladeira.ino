/*
  Tô Bem — nó da geladeira ("já tomou café?") — protótipo GS 2026/2
  ------------------------------------------------------------------------
  Segundo dispositivo IoT da casa: ESP32 + sensor magnético MC-38 (reed switch)
  na porta da geladeira. Fica na caixa 3D (hardware-3d/caixa-geladeira), ligado
  a uma fonte USB 5V na tomada.

  Envia para a MESMA API da lâmpada (POST /api/eventos), com outro dispositivoId:
    INICIADO          -> ao ligar
    GELADEIRA_ABERTA  -> cada vez que a porta abre
    HEARTBEAT         -> periódico; valor = sinal Wi-Fi em dBm (0 = sem Wi-Fi)

  Ligação do MC-38 (não tem polaridade):
    fio 1 -> GPIO 27      fio 2 -> GND      (o ímã vai na porta, o sensor no corpo)
    Porta fechada: ímã perto, contato fechado -> GPIO 27 em LOW
    Porta aberta : ímã longe, contato aberto  -> GPIO 27 em HIGH (pull-up interno)
*/

#include <WiFi.h>
#include <HTTPClient.h>

// Credenciais: copie "secrets.example.h" para "secrets.h" e preencha (o secrets.h não vai para o GitHub).
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif
const char* WIFI_SSID  = SECRET_WIFI_SSID;
const char* WIFI_SENHA = SECRET_WIFI_SENHA;
const char* API_URL    = SECRET_API_URL;   // vazio = só imprime no Serial
const int   DISPOSITIVO_ID = 2;            // 1 = lâmpada, 2 = geladeira

#define PIN_REED  27
#define PIN_LED   2      // LED azul da placa: pisca quando a porta abre

const unsigned long HEARTBEAT_MS     = 10000;  // demo: 10 s | produto: 60 s
const unsigned long DEBOUNCE_MS      = 80;     // ignora o "tremido" do contato
const unsigned long HTTP_TIMEOUT_MS  = 2000;

bool portaAberta = false, leituraAnterior = false;
unsigned long mudouEm = 0, ultimoHeartbeat = 0, ledAte = 0;

String agoraIso() {
  struct tm t;
  if (!getLocalTime(&t, 10)) return "";
  char buf[30];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S-03:00", &t);
  return String(buf);
}

void enviarEvento(const char* tipo, float valor = 1) {
  String dataHora = agoraIso();
  String json = "{\"dispositivoId\":";
  json += DISPOSITIVO_ID;
  json += ",\"tipo\":\"";
  json += tipo;
  json += "\",\"valor\":";
  json += String(valor, 1);
  if (dataHora.length()) { json += ",\"dataHora\":\""; json += dataHora; json += "\""; }
  json += "}";
  Serial.println("[EVENTO] " + json);
  if (strlen(API_URL) == 0 || WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  unsigned long t0 = millis();
  int code = http.POST(json);
  Serial.printf("         -> API respondeu %d em %lu ms\n", code, millis() - t0);
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\nTô Bem — nó da geladeira iniciando");
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\nWi-Fi conectado | IP %s | RSSI %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  else
    Serial.println("\nSem Wi-Fi: só Serial (o ESP32 tenta reconectar sozinho)");
  configTime(-3 * 3600, 0, "pool.ntp.org");

  portaAberta = leituraAnterior = digitalRead(PIN_REED) == HIGH;
  Serial.printf("Porta começou %s\n", portaAberta ? "ABERTA" : "fechada");
  enviarEvento("INICIADO");
}

void loop() {
  unsigned long agora = millis();

  // Porta: só aceita a mudança se ela ficar estável por DEBOUNCE_MS
  bool lido = digitalRead(PIN_REED) == HIGH;
  if (lido != leituraAnterior) { leituraAnterior = lido; mudouEm = agora; }
  if (agora - mudouEm > DEBOUNCE_MS && lido != portaAberta) {
    portaAberta = lido;
    if (portaAberta) {
      Serial.println("Porta da geladeira ABERTA");
      enviarEvento("GELADEIRA_ABERTA");
      ledAte = millis() + 1000;
    } else {
      Serial.println("Porta da geladeira fechada");
    }
  }
  digitalWrite(PIN_LED, millis() < ledAte ? HIGH : LOW);

  if (agora - ultimoHeartbeat > HEARTBEAT_MS) {
    enviarEvento("HEARTBEAT", WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
    ultimoHeartbeat = agora;
  }
  delay(10);
}
