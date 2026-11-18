/*
  Tô Bem — firmware da lâmpada (protótipo GS 2026/2) — versão HARDWARE REAL
  ------------------------------------------------------------------------
  Radar: HLK-LD2410C (padrão) ou HLK-LD2420 — escolha em RADAR_MODELO.
    LD2410C: informa sozinho "em movimento" e "parado" + distâncias (biblioteca "ld2410", ncmreynolds).
    LD2420 : informa presença + distância; o movimento é deduzido pela variação da distância.
 
  As 3 coisas separadas:
    1) Sensor (radar)  -> sempre ligado, não depende da luz
    2) Luz principal   -> só a pessoa controla (botão "Luz")
    3) Luz noturna     -> automática, discreta, só de madrugada com a luz apagada
 
  Prioridade dos anéis: ALERTA > feedback de botão > luz principal > luz noturna > apagado
*/
 
#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>
#include "DHTesp.h"
 
// ============================ CONFIGURAÇÃO ============================
#define RADAR_MODELO 2410        // 2410 = HLK-LD2410C (5V)  |  2420 = HLK-LD2420 (3V3)
const char* WIFI_SSID  = "SUA_REDE";      // não suba a senha real para o GitHub
const char* WIFI_SENHA = "SUA_SENHA";
const char* API_URL    = "";              // ex.: "http://IP-DA-VM:8080/api/eventos" — vazio = só imprime no Serial
const int   DISPOSITIVO_ID = 1;
 
const bool DEBUG_RADAR      = false;   // imprime tudo que o radar manda (desligue depois de calibrar)
const bool DEMO_MADRUGADA   = true;   // true = considera sempre madrugada (para testar a luz noturna)
 
// Tempos — valores CURTOS para demonstração (produto: ver comentários)
const unsigned long TEMPO_QUEDA_MS        = 20000;  // parado no chão (produto: 20 min)
const unsigned long TEMPO_RESPOSTA_MS     = 15000;  // tempo para apertar "estou bem" (produto: 2 min)
const unsigned long TEMPO_INATIVIDADE_MS  = 60000;  // ninguém detectado (produto: hora habitual + 2 h)
const unsigned long DURACAO_NOTURNA_MS    = 15000;  // luz noturna acesa (produto: 3 min)
const unsigned long JANELA_MOVIMENTO_MS   = 600000; // queda só conta se houve movimento nos últimos 10 min
const unsigned long HEARTBEAT_MS          = 10000;  // produto: 60 s
const unsigned long TEMPERATURA_MS        = 30000;
const unsigned long TEMPO_CALOR_MS        = 10000;
 
// Radar: como decidir "movimento" (só LD2420) e "chão" (os dois)
const int  LIMIAR_MOV_CM   = 15;      // variação de distância que conta como movimento
const unsigned long MOV_JANELA_MS = 1500; // considera "movendo" até 1,5 s após a última variação
const int  LIMIAR_CHAO_CM  = 200;     // teto real (~2,6 m): >= 200 cm = chão.
                                      // Na maquete (lâmpada a ~50 cm), use algo como 35.
const int  HISTERESE_CM    = 15;      // para sair do "chão", a distância precisa cair 15 cm abaixo do limiar
const float SUAVIZACAO_MS     = 1500;     // média móvel da distância: 0.2 = suave, 1.0 = sem filtro
const unsigned long INTERVALO_EVENTO_MOV_MS = 15000;  // manda MOVIMENTO no máximo a cada 15 s
const float LIMIAR_CALOR_C = 32.0;
const unsigned long INTERVALO_STATUS_MS = 1000;   // uma linha de status por segundo
 
// Brilho: no USB do computador use BAIXO (40). Com a fonte 5V 3A pode subir para 90.
const uint8_t BRILHO_MAX = 40;
 
// ============================ PINOS (conforme a montagem do grupo) ============================
// Anéis de LED — ATENÇÃO: GPIO 34 e 35 são SÓ DE ENTRADA no ESP32 e não conseguem
// mandar dados para os LEDs. Mova os fios para os pinos vizinhos na placa:
//   fio que estava no D34 ("in led")  -> D33
//   fio que estava no D35 ("out led") -> D25
#define PIN_ANEL1      33     // DIN do anel 1 (antes no 34)
#define PIN_ANEL2      25     // DIN do anel 2 (antes no 35). Se os anéis forem encadeados DOUT1->DIN2, coloque -1
#define LEDS_POR_ANEL  16
 
// Radar — ligação da montagem do grupo (mesmos pinos para os dois modelos)
//   LD2410C: TX -> D21 | RX -> D19 | OUT -> D18 | VCC -> 5V (VIN) | GND -> GND
//   LD2420 : OT1-> D21 | RX -> D19 | OT2 -> D18 | 3V3 -> 3V3      | GND -> GND
#define PIN_RADAR_RX   21     // ESP32 recebe  <- TX do radar
#define PIN_RADAR_TX   19     // ESP32 envia   -> RX do radar
#define PIN_RADAR_OUT  18     // saída digital de presença (OUT no LD2410C / OT2 no LD2420)
#if RADAR_MODELO == 2410
  #define RADAR_BAUD   256000
  #include <ld2410.h>
  ld2410 radar;
#else
  #define RADAR_BAUD   115200
#endif
 
#define PIN_DHT        32     // DHT22 (dados)               -> D32
 
// Base (ainda não montada) — pinos livres reservados
#define PIN_BTN_OK     13     // verde  "Estou bem"
#define PIN_BTN_AWAY   14     // azul   "Saí / Voltei"
#define PIN_BTN_LUZ    4      // branco "Luz"
#define PIN_BUZZER     26
#define PIN_REED       27     // geladeira (MC-38)
 
// ============================ OBJETOS ============================
const int TOTAL_ANEL1 = (PIN_ANEL2 < 0) ? LEDS_POR_ANEL * 2 : LEDS_POR_ANEL;
Adafruit_NeoPixel anel1(TOTAL_ANEL1, PIN_ANEL1, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel anel2(LEDS_POR_ANEL, (PIN_ANEL2 < 0 ? PIN_ANEL1 : PIN_ANEL2), NEO_GRB + NEO_KHZ800);
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
float distSuave = -1;          // distância filtrada (média móvel)
bool  noChao = false;          // estado "altura do chão" com histerese
unsigned long ultimoEventoMov = 0;
 
// Radar: estado montado a partir das mensagens
bool radarPresencaSerial = false;
int  radarDistancia = 0, distanciaReferencia = -1;
unsigned long ultimaMudancaDist = 0, ultimaMsgRadar = 0;
char linhaRadar[64]; uint8_t posLinha = 0;
 
// ============================ BOTÕES (debounce) ============================
struct Botao { uint8_t pino; bool estado; bool ultimoLido; unsigned long mudou; };
Botao btnOk{PIN_BTN_OK, HIGH, HIGH, 0}, btnAway{PIN_BTN_AWAY, HIGH, HIGH, 0}, btnLuz{PIN_BTN_LUZ, HIGH, HIGH, 0};
 
bool apertou(Botao& b) {
  bool lido = digitalRead(b.pino);
  if (lido != b.ultimoLido) { b.mudou = millis(); b.ultimoLido = lido; }
  if (millis() - b.mudou > 40 && lido != b.estado) {
    b.estado = lido;
    if (lido == LOW) return true;
  }
  return false;
}
 
// ============================ UTILITÁRIOS ============================
uint32_t cor(uint8_t r, uint8_t g, uint8_t b) { return anel1.Color(r, g, b); }
 
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
  if (DEMO_MADRUGADA) return true;
  struct tm t;
  if (getLocalTime(&t, 10)) return (t.tm_hour >= 22 || t.tm_hour < 6);
  return false;
}
 
void bip(uint16_t freq, uint16_t ms) { tone(PIN_BUZZER, freq, ms); }
 
// ============================ RADAR LD2420 ============================
// O LD2420 (firmware padrão) manda linhas de texto como:
//   "ON"            -> alguém detectado
//   "OFF"           -> ninguém
//   "Range 123"     -> distância do alvo (cm)
// Se o seu firmware mandar outro formato, ligue DEBUG_RADAR e veja no Serial.
void processarLinhaRadar(const char* l) {
  ultimaMsgRadar = millis();
  if (DEBUG_RADAR) Serial.printf("[RADAR] %s\n", l);
  if (strncmp(l, "ON", 2) == 0)  radarPresencaSerial = true;
  if (strncmp(l, "OFF", 3) == 0) radarPresencaSerial = false;
  const char* p = strstr(l, "Range");
  if (p) {
    p += 5;
    while (*p == ' ' || *p == ':') p++;
    int d = atoi(p);
    if (d >= 0 && d < 1000) { radarDistancia = d; radarPresencaSerial = true; }
  }
}
 
void lerSerialRadar() {
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\r') continue;
    if (c == '\n') {
      linhaRadar[posLinha] = 0;
      if (posLinha > 0) processarLinhaRadar(linhaRadar);
      posLinha = 0;
    } else if (c >= 32 && c < 127 && posLinha < sizeof(linhaRadar) - 1) {
      linhaRadar[posLinha++] = c;
    }
  }
}
 
LeituraRadar lerRadar() {
  LeituraRadar r{false, false, 0};
#if RADAR_MODELO == 2410
  radar.read();
  if (radar.isConnected()) ultimaMsgRadar = millis();
  r.presente = radar.presenceDetected() || digitalRead(PIN_RADAR_OUT) == HIGH;
  r.movendo  = radar.movingTargetDetected();
  if (r.movendo)                           r.distanciaCm = radar.movingTargetDistance();
  else if (radar.stationaryTargetDetected()) r.distanciaCm = radar.stationaryTargetDistance();
  if (DEBUG_RADAR) {
    static unsigned long ultimoDebug = 0;
    if (millis() - ultimoDebug > 1000) {
      ultimoDebug = millis();
      Serial.printf("[RADAR] mov=%d dist=%dcm energia=%d | parado=%d dist=%dcm energia=%d\n",
                    radar.movingTargetDetected(), radar.movingTargetDistance(), radar.movingTargetEnergy(),
                    radar.stationaryTargetDetected(), radar.stationaryTargetDistance(), radar.stationaryTargetEnergy());
    }
  }
#else
  lerSerialRadar();
  bool presencaPino = digitalRead(PIN_RADAR_OUT) == HIGH;
  r.presente = presencaPino || radarPresencaSerial;
  r.distanciaCm = r.presente ? radarDistancia : 0;
  unsigned long agora = millis();
  if (r.presente) {
    if (distanciaReferencia < 0 || abs(radarDistancia - distanciaReferencia) >= LIMIAR_MOV_CM) {
      distanciaReferencia = radarDistancia;
      ultimaMudancaDist = agora;
    }
    r.movendo = (agora - ultimaMudancaDist) < MOV_JANELA_MS;
  } else {
    distanciaReferencia = -1;
  }
#endif
  // Média móvel: tira a oscilação natural da leitura
  static unsigned long ultimaSuav = 0;
  unsigned long agoraF = millis();
  float dt = (ultimaSuav == 0) ? 0 : (float)(agoraF - ultimaSuav);
  ultimaSuav = agoraF;
  if (r.presente && r.distanciaCm > 0) {
    float alfa = dt / (SUAVIZACAO_MS + dt);
    distSuave = (distSuave < 0) ? r.distanciaCm : distSuave + alfa * (r.distanciaCm - distSuave);
    r.distanciaCm = (int)(distSuave + 0.5);
  } else if (!r.presente) {
    distSuave = -1;
  }

  // Histerese: entra no "chão" no limiar e só sai 15 cm abaixo dele
  if (!r.presente) noChao = false;
  else if (!noChao && r.distanciaCm >= LIMIAR_CHAO_CM) noChao = true;
  else if (noChao && r.distanciaCm < LIMIAR_CHAO_CM - HISTERESE_CM) noChao = false;
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
  feedbackCor = cor(0, 180, 0); feedbackAte = millis() + 1200;
}
 
void atualizarAlerta() {
  if (alerta == SEM_ALERTA) return;
  if (nivel == NIVEL_1_CASA && millis() - alertaInicio > TEMPO_RESPOSTA_MS) {
    nivel = NIVEL_2_FAMILIA;
    Serial.printf(">>> Sem resposta: alerta %s escalado para a FAMÍLIA (nível 2)\n", nomeAlerta(alerta));
    enviarEvento(alerta == ALERTA_QUEDA ? "ALERTA_QUEDA_N2" : "ALERTA_INATIVIDADE_N2");
  }
  static unsigned long ultimoBip = 0;
  if (millis() - ultimoBip >= 1000) {
    ultimoBip = millis();
    if (nivel == NIVEL_1_CASA) bip(1500, 150); else bip(2200, 500);
  }
}
 
// ============================ ANÉIS DE LED ============================
void pintar(uint32_t c) {
  for (int i = 0; i < TOTAL_ANEL1; i++) anel1.setPixelColor(i, c);
  anel1.show();
  if (PIN_ANEL2 >= 0) {
    for (int i = 0; i < LEDS_POR_ANEL; i++) anel2.setPixelColor(i, c);
    anel2.show();
  }
}
 
void desenharAneis() {
  uint32_t c = 0;
  bool pisca = (millis() / 400) % 2;
  if (alerta != SEM_ALERTA)        c = pisca ? (nivel == NIVEL_1_CASA ? cor(255, 170, 0) : cor(255, 0, 0)) : 0;
  else if (millis() < feedbackAte) c = feedbackCor;
  else if (luzPrincipal)           c = cor(255, 255, 230);
  else if (millis() < noturnaAte)  c = cor(60, 25, 0);
  pintar(c);
}
 
void testePartidaAneis() {
  int total = TOTAL_ANEL1 + (PIN_ANEL2 >= 0 ? LEDS_POR_ANEL : 0);
  for (int i = 0; i < total; i++) {
    if (i < TOTAL_ANEL1) { anel1.setPixelColor(i, cor(0, 120, 255)); anel1.show(); }
    else { anel2.setPixelColor(i - TOTAL_ANEL1, cor(0, 120, 255)); anel2.show(); }
    delay(30);
  }
  delay(400);
  pintar(0);
}
 
// ============================ SETUP ============================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nTô Bem — iniciando (hardware real, radar LD%d)\n", RADAR_MODELO);
 
  pinMode(PIN_BTN_OK, INPUT_PULLUP);
  pinMode(PIN_BTN_AWAY, INPUT_PULLUP);
  pinMode(PIN_BTN_LUZ, INPUT_PULLUP);
  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_RADAR_OUT, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);
 
  anel1.begin(); anel1.setBrightness(BRILHO_MAX); anel1.clear(); anel1.show();
  if (PIN_ANEL2 >= 0) { anel2.begin(); anel2.setBrightness(BRILHO_MAX); anel2.clear(); anel2.show(); }
  testePartidaAneis();
 
  dht.setup(PIN_DHT, DHTesp::DHT22);
  Serial2.begin(RADAR_BAUD, SERIAL_8N1, PIN_RADAR_RX, PIN_RADAR_TX);
#if RADAR_MODELO == 2410
  delay(500);
  Serial.println(radar.begin(Serial2) ? "Radar LD2410C conectado" : "Radar LD2410C NÃO respondeu — confira TX/RX (D21/D19) e o 5V");
#endif
 
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
    if (!movendoAntes && agora - ultimoEventoMov > INTERVALO_EVENTO_MOV_MS) {
      enviarEvento("MOVIMENTO", r.distanciaCm);
      ultimoEventoMov = agora;
    }
    ultimoMovimento = agora;
    if (modoAusente) { modoAusente = false; Serial.println("Presença detectada: modo ausente desligado"); enviarEvento("BOTAO_VOLTEI"); }
  }
  movendoAntes = r.movendo;
 
  bool paradoNoChao = r.presente && !r.movendo && noChao;
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
 
  if (!modoAusente && alerta == SEM_ALERTA && agora - ultimaPresenca > TEMPO_INATIVIDADE_MS) {
    enviarEvento("SEM_PRESENCA");
    iniciarAlerta(ALERTA_INATIVIDADE);
  }
  if (alerta == ALERTA_INATIVIDADE && r.movendo) resolverAlerta("movimento detectado", "ALERTA_RESOLVIDO_MOVIMENTO");
 
  atualizarAlerta();
 
  // ---------- 2) LUZ PRINCIPAL ----------
  if (apertou(btnLuz)) {
    luzPrincipal = !luzPrincipal;
    Serial.printf("Luz principal %s (radar continua ligado)\n", luzPrincipal ? "LIGADA" : "DESLIGADA");
    enviarEvento(luzPrincipal ? "LUZ_LIGADA" : "LUZ_DESLIGADA");
  }
 
  // ---------- 3) LUZ NOTURNA ----------
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
  if (agora - ultimaTemp > 2500) {
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
 
  // ---------- Status ----------
  if (agora - ultimoStatus >= INTERVALO_STATUS_MS) {
    const char* zona = !r.presente ? "ninguém" : (noChao ? "CHÃO" : "sentado/em pé");
    bool radarFalando = (agora - ultimaMsgRadar) < 5000;
    Serial.printf("[STATUS] OUT=%d serial=%s | presença=%d mov=%d dist=%dcm (%s) | luz=%s noturna=%s | %.1f°C | alerta=%s nível=%d\n",
                  digitalRead(PIN_RADAR_OUT), radarFalando ? "ok" : "SEM DADOS",
                  r.presente, r.movendo, r.distanciaCm, zona,
                  luzPrincipal ? "ON" : "off", millis() < noturnaAte ? "ON" : "off",
                  temperatura, nomeAlerta(alerta), nivel);
    ultimoStatus = agora;
  }
 
  desenharAneis();
  delay(20);
}