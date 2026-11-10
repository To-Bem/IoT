/*
  Tô Bem — firmware da lâmpada (protótipo GS 2026/2)
  ---------------------------------------------------
  Simulação no Wokwi: o radar LD2410C ainda não existe no Wokwi, então ele é
  simulado por 2 chaves deslizantes (presença / movimento) e 1 potenciômetro
  (distância do alvo até o teto). Quando o radar real chegar, troque
  USAR_RADAR_REAL para 1 — o resto do código não muda.

  As 3 coisas separadas:
    1) Sensor (radar)  -> sempre ligado, não depende da luz
    2) Luz principal   -> só a pessoa controla (botão "Luz")
    3) Luz noturna     -> automática, discreta, só de madrugada com a luz apagada

  Prioridade do anel de LED: ALERTA > feedback de botão > luz principal > luz noturna > apagado
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include "DHTesp.h"

// ============================ CONFIGURAÇÃO ============================
#define USAR_RADAR_REAL 0          // 0 = simulado (Wokwi) | 1 = LD2410C real

const char* WIFI_SSID  = "Wokwi-GUEST";   // no hardware real: Wi-Fi da casa
const char* WIFI_SENHA = "";
const char* API_URL    = "";              // ex.: "http://IP-DA-VM:8080/api/eventos" — vazio = só imprime no Serial
const int   DISPOSITIVO_ID = 1;

// Tempos — valores CURTOS para demonstração (produto: ver comentários)
const unsigned long TEMPO_QUEDA_MS        = 20000;  // parado no chão (produto: 20 min)
const unsigned long TEMPO_RESPOSTA_MS     = 15000;  // tempo para apertar "estou bem" (produto: 2 min)
const unsigned long TEMPO_INATIVIDADE_MS  = 60000;  // ninguém detectado (produto: hora habitual + 2 h)
const unsigned long DURACAO_NOTURNA_MS    = 15000;  // luz noturna acesa (produto: 3 min)
const unsigned long JANELA_MOVIMENTO_MS   = 600000; // queda só conta se houve movimento nos últimos 10 min
const unsigned long HEARTBEAT_MS          = 10000;  // produto: 60 s
const unsigned long TEMPERATURA_MS        = 30000;  // envio periódico da temperatura
const unsigned long TEMPO_CALOR_MS        = 10000;  // temperatura alta sustentada

// Geometria: radar no teto (~2,6 m). Distância medida do radar até a pessoa.
const int LIMIAR_CHAO_CM = 200;   // >= 200 cm do teto = altura do chão (deitado)
const float LIMIAR_CALOR_C = 32.0;

const uint8_t BRILHO_MAX = 90;    // ~35% de 255 (limite de corrente/calor)

// ============================ PINOS ============================
// Lâmpada
#define PIN_LED_RING   26
#define LED_COUNT      16
#define PIN_RADAR_RX   16   // RX2 <- TX do LD2410C
#define PIN_RADAR_TX   17   // TX2 -> RX do LD2410C
// Simulação do radar (somente Wokwi)
#define PIN_SIM_PRESENCA   32   // chave: alvo presente
#define PIN_SIM_MOVIMENTO  14   // chave: alvo em movimento
#define PIN_SIM_DISTANCIA  34   // potenciômetro: 0–300 cm do teto
#define PIN_SIM_MADRUGADA  13   // chave: força "madrugada" na demo
// Base (cabo de 8 vias)
#define PIN_BTN_OK     18   // verde  "Estou bem"
#define PIN_BTN_AWAY   19   // azul   "Saí / Voltei"
#define PIN_BTN_LUZ    27   // branco "Luz"
#define PIN_BUZZER     25
#define PIN_DHT        4
#define PIN_REED       33   // geladeira (no Wokwi: chave deslizante)

#if USAR_RADAR_REAL
  #include <ld2410.h>
  ld2410 radar;
#endif

Adafruit_NeoPixel anel(LED_COUNT, PIN_LED_RING, NEO_GRB + NEO_KHZ800);
DHTesp dht;

// ============================ ESTADO ============================
struct LeituraRadar { bool presente; bool movendo; int distanciaCm; };

enum TipoAlerta { SEM_ALERTA, ALERTA_QUEDA, ALERTA_INATIVIDADE };
enum NivelAlerta { NIVEL_0, NIVEL_1_CASA, NIVEL_2_FAMILIA };

bool luzPrincipal = false;
bool modoAusente  = false;
TipoAlerta  alerta = SEM_ALERTA;
NivelAlerta nivel  = NIVEL_0;
unsigned long alertaInicio = 0;

unsigned long ultimoMovimento = 0, ultimaPresenca = 0;
unsigned long paradoNoChaoDesde = 0;
unsigned long noturnaAte = 0;
unsigned long feedbackAte = 0; uint32_t feedbackCor = 0;
unsigned long ultimoHeartbeat = 0, ultimaTemp = 0, calorDesde = 0, ultimoStatus = 0;
bool calorAvisado = false, movendoAntes = false, geladeiraAbertaAntes = false;
float temperatura = NAN, umidade = NAN;

// ============================ BOTÕES (debounce) ============================
struct Botao { uint8_t pino; bool estado; bool ultimoLido; unsigned long mudou; };
Botao btnOk{PIN_BTN_OK, HIGH, HIGH, 0}, btnAway{PIN_BTN_AWAY, HIGH, HIGH, 0}, btnLuz{PIN_BTN_LUZ, HIGH, HIGH, 0};

bool apertou(Botao& b) {             // true uma vez, na borda de descida
  bool lido = digitalRead(b.pino);
  if (lido != b.ultimoLido) { b.mudou = millis(); b.ultimoLido = lido; }
  if (millis() - b.mudou > 40 && lido != b.estado) {
    b.estado = lido;
    if (lido == LOW) return true;
  }
  return false;
}

// ============================ UTILITÁRIOS ============================
uint32_t cor(uint8_t r, uint8_t g, uint8_t b) { return anel.Color(r, g, b); }

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
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(json);
  Serial.printf("         -> API respondeu %d\n", code);
  http.end();
}

bool ehMadrugada() {
  if (digitalRead(PIN_SIM_MADRUGADA) == LOW) return true;   // chave da demo
  struct tm t;
  if (getLocalTime(&t, 10)) return (t.tm_hour >= 22 || t.tm_hour < 6);
  return false;
}

void bip(uint16_t freq, uint16_t ms) { tone(PIN_BUZZER, freq, ms); }

// ============================ RADAR ============================
LeituraRadar lerRadar() {
  LeituraRadar r{false, false, 0};
#if USAR_RADAR_REAL
  radar.read();
  r.presente = radar.presenceDetected();
  r.movendo  = radar.movingTargetDetected();
  r.distanciaCm = r.movendo ? radar.movingTargetDistance() : radar.stationaryTargetDistance();
#else
  r.presente    = digitalRead(PIN_SIM_PRESENCA) == LOW;
  r.movendo     = r.presente && digitalRead(PIN_SIM_MOVIMENTO) == LOW;
  r.distanciaCm = map(analogRead(PIN_SIM_DISTANCIA), 0, 4095, 0, 300);
#endif
  return r;
}

// ============================ ALERTAS ============================
const char* nomeAlerta(TipoAlerta a) {
  return a == ALERTA_QUEDA ? "QUEDA" : a == ALERTA_INATIVIDADE ? "INATIVIDADE" : "-";
}

void iniciarAlerta(TipoAlerta tipo) {
  alerta = tipo; nivel = NIVEL_1_CASA; alertaInicio = millis();
  Serial.printf(">>> ALERTA %s — nível 1 (casa): aperte o botão verde\n", nomeAlerta(tipo));
  enviarEvento(tipo == ALERTA_QUEDA ? "ALERTA_QUEDA_N1" : "ALERTA_INATIVIDADE_N1");
}

void resolverAlerta(const char* motivo, const char* evento) {
  Serial.printf("<<< Alerta %s resolvido (%s)\n", nomeAlerta(alerta), motivo);
  enviarEvento(evento);
  alerta = SEM_ALERTA; nivel = NIVEL_0; paradoNoChaoDesde = 0;
  ultimaPresenca = millis();
  noTone(PIN_BUZZER);
  feedbackCor = cor(0, 180, 0); feedbackAte = millis() + 1200;    // verde = ok
}

void atualizarAlerta() {
  if (alerta == SEM_ALERTA) return;
  if (nivel == NIVEL_1_CASA && millis() - alertaInicio > TEMPO_RESPOSTA_MS) {
    nivel = NIVEL_2_FAMILIA;
    Serial.printf(">>> Sem resposta: alerta %s escalado para a FAMÍLIA (nível 2)\n", nomeAlerta(alerta));
    enviarEvento(alerta == ALERTA_QUEDA ? "ALERTA_QUEDA_N2" : "ALERTA_INATIVIDADE_N2");
  }
  // buzzer: nível 1 = bipe curto por segundo | nível 2 = bipe longo intermitente
  static unsigned long ultimoBip = 0;
  if (millis() - ultimoBip >= 1000) {
    ultimoBip = millis();
    if (nivel == NIVEL_1_CASA) bip(1500, 150); else bip(2200, 500);
  }
}

// ============================ ANEL DE LED ============================
void desenharAnel() {
  uint32_t c = 0;
  bool pisca = (millis() / 400) % 2;
  if (alerta != SEM_ALERTA) {                                   // 1) alerta
    c = pisca ? (nivel == NIVEL_1_CASA ? cor(255, 170, 0) : cor(255, 0, 0)) : 0;
  } else if (millis() < feedbackAte) {                          // 2) feedback de botão
    c = feedbackCor;
  } else if (luzPrincipal) {                                    // 3) luz principal
    c = cor(255, 255, 230);
  } else if (millis() < noturnaAte) {                           // 4) luz noturna (âmbar fraco)
    c = cor(60, 25, 0);
  }
  for (int i = 0; i < LED_COUNT; i++) anel.setPixelColor(i, c);
  anel.show();
}

// ============================ SETUP ============================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BTN_OK, INPUT_PULLUP);
  pinMode(PIN_BTN_AWAY, INPUT_PULLUP);
  pinMode(PIN_BTN_LUZ, INPUT_PULLUP);
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_SIM_PRESENCA, INPUT_PULLUP);
  pinMode(PIN_SIM_MOVIMENTO, INPUT_PULLUP);
  pinMode(PIN_SIM_MADRUGADA, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);

  anel.begin(); anel.setBrightness(BRILHO_MAX); anel.clear(); anel.show();
  dht.setup(PIN_DHT, DHTesp::DHT22);

#if USAR_RADAR_REAL
  Serial2.begin(256000, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
  radar.begin(Serial2);
#endif

  Serial.println("\nTô Bem — iniciando");
  WiFi.begin(WIFI_SSID, WIFI_SENHA);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) { delay(250); Serial.print("."); }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWi-Fi conectado" : "\nSem Wi-Fi: funcionando só localmente");
  configTime(-3 * 3600, 0, "pool.ntp.org");

  ultimaPresenca = ultimoMovimento = millis();
  geladeiraAbertaAntes = digitalRead(PIN_REED) == HIGH;
  enviarEvento("INICIADO");
}

// ============================ LOOP ============================
void loop() {
  unsigned long agora = millis();
  LeituraRadar r = lerRadar();

  // ---------- 1) SENSOR: sempre ligado ----------
  if (r.presente) ultimaPresenca = agora;
  if (r.movendo) {
    if (!movendoAntes) enviarEvento("MOVIMENTO", r.distanciaCm);
    ultimoMovimento = agora;
    if (modoAusente) { modoAusente = false; Serial.println("Presença detectada: modo ausente desligado"); enviarEvento("BOTAO_VOLTEI"); }
  }
  movendoAntes = r.movendo;

  // Possível queda: parado + altura do chão + houve movimento antes
  bool paradoNoChao = r.presente && !r.movendo && r.distanciaCm >= LIMIAR_CHAO_CM;
  if (paradoNoChao) {
    if (paradoNoChaoDesde == 0) paradoNoChaoDesde = agora;
    bool houveMovimentoAntes = (paradoNoChaoDesde - ultimoMovimento) < JANELA_MOVIMENTO_MS;
    if (alerta == SEM_ALERTA && houveMovimentoAntes && agora - paradoNoChaoDesde > TEMPO_QUEDA_MS) {
      enviarEvento("PRESENCA_PARADA", r.distanciaCm);
      iniciarAlerta(ALERTA_QUEDA);
    }
  } else {
    paradoNoChaoDesde = 0;
  }

  // Sem sinal de vida (desligado no modo ausente)
  if (!modoAusente && alerta == SEM_ALERTA && agora - ultimaPresenca > TEMPO_INATIVIDADE_MS) {
    enviarEvento("SEM_PRESENCA");
    iniciarAlerta(ALERTA_INATIVIDADE);
  }
  // Se a pessoa voltou a se mexer durante um alerta de inatividade, resolve sozinho
  if (alerta == ALERTA_INATIVIDADE && r.movendo) resolverAlerta("movimento detectado", "ALERTA_RESOLVIDO_MOVIMENTO");

  atualizarAlerta();

  // ---------- 2) LUZ PRINCIPAL: só a pessoa controla ----------
  if (apertou(btnLuz)) {
    luzPrincipal = !luzPrincipal;
    Serial.printf("Luz principal %s (radar continua ligado)\n", luzPrincipal ? "LIGADA" : "DESLIGADA");
    enviarEvento(luzPrincipal ? "LUZ_LIGADA" : "LUZ_DESLIGADA");
  }

  // ---------- 3) LUZ NOTURNA: automática e discreta ----------
  if (!luzPrincipal && r.movendo && ehMadrugada()) noturnaAte = agora + DURACAO_NOTURNA_MS;

  // ---------- Botões da base ----------
  if (apertou(btnOk)) {
    if (alerta != SEM_ALERTA) resolverAlerta("botão verde", "BOTAO_OK");
    else { Serial.println("Estou bem (check-in)"); enviarEvento("BOTAO_OK"); feedbackCor = cor(0, 180, 0); feedbackAte = agora + 1200; }
  }
  if (apertou(btnAway)) {
    modoAusente = !modoAusente;
    Serial.printf("Modo ausente %s\n", modoAusente ? "ATIVADO (saí)" : "DESATIVADO (voltei)");
    enviarEvento(modoAusente ? "BOTAO_AUSENTE" : "BOTAO_VOLTEI");
    feedbackCor = cor(0, 80, 255); feedbackAte = agora + 1200;
    ultimaPresenca = agora;
  }

  // ---------- Geladeira ----------
  bool geladeiraAberta = digitalRead(PIN_REED) == HIGH;
  if (geladeiraAberta && !geladeiraAbertaAntes) enviarEvento("GELADEIRA_ABERTA");
  geladeiraAbertaAntes = geladeiraAberta;

  // ---------- Temperatura ----------
  if (agora - ultimaTemp > 2500) {                 // DHT22: no máximo 1 leitura a cada 2 s
    TempAndHumidity th = dht.getTempAndHumidity();
    if (!isnan(th.temperature)) { temperatura = th.temperature; umidade = th.humidity; }
    if (temperatura >= LIMIAR_CALOR_C) {
      if (calorDesde == 0) calorDesde = agora;
      if (!calorAvisado && agora - calorDesde > TEMPO_CALOR_MS) {
        calorAvisado = true;
        Serial.printf(">>> Calor perigoso: %.1f °C\n", temperatura);
        enviarEvento("TEMPERATURA_ALTA", temperatura);
        for (int i = 0; i < 3; i++) { bip(1000, 120); delay(200); }
      }
    } else if (temperatura < LIMIAR_CALOR_C - 1) { calorDesde = 0; calorAvisado = false; }
    ultimaTemp = agora;
  }
  static unsigned long ultimaTempEnviada = 0;
  if (agora - ultimaTempEnviada > TEMPERATURA_MS && !isnan(temperatura)) { enviarEvento("TEMPERATURA", temperatura); ultimaTempEnviada = agora; }

  // ---------- Heartbeat ----------
  if (agora - ultimoHeartbeat > HEARTBEAT_MS) { enviarEvento("HEARTBEAT"); ultimoHeartbeat = agora; }

  // ---------- Status no Serial (evidência para a disciplina de IoT) ----------
  if (agora - ultimoStatus > 3000) {
    const char* zona = !r.presente ? "ninguém" : (r.distanciaCm >= LIMIAR_CHAO_CM ? "CHÃO" : "sentado/em pé");
    Serial.printf("[STATUS] presença=%d mov=%d dist=%dcm (%s) | luz=%s noturna=%s ausente=%d | %.1f°C | alerta=%s nível=%d\n",
                  r.presente, r.movendo, r.distanciaCm, zona,
                  luzPrincipal ? "ON" : "off", millis() < noturnaAte ? "ON" : "off", modoAusente,
                  temperatura, nomeAlerta(alerta), nivel);
    ultimoStatus = agora;
  }

  desenharAnel();
  delay(20);
}
