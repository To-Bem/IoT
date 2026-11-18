# Justificativas técnicas — Tô Bem

Este documento atende ao requisito **06: documentação técnica e justificativa das escolhas**. Cada decisão traz as alternativas que o grupo considerou e o que se ganha e se perde com ela.

## 1. Hardware

| Decisão | Alternativas consideradas | Por que escolhemos | O que perdemos |
|---|---|---|---|
| **ESP32 DevKit V1, programado com o Arduino core** | Arduino Uno + módulo Wi-Fi ESP-01; Arduino Uno R4 WiFi; Raspberry Pi | Wi-Fi 2,4 GHz e Bluetooth integrados, 240 MHz e 2 núcleos, várias UARTs, baixo custo. Usa o **ecossistema Arduino** (IDE, bibliotecas, `setup()`/`loop()`), como pede a disciplina | Lógica de 3,3 V (o radar e os LEDs usam 5 V na alimentação); GPIO 34–39 só de entrada |
| **Radar mmWave HLK-LD2410C** | PIR HC-SR501; câmera com visão computacional; HLK-LD2420 | Detecta a pessoa **parada** pela respiração e micro-movimentos (o PIR só vê movimento), mede a distância (dá para separar "chão" de "sentado") e informa sozinho "em movimento" e "parado". Não captura imagem: privacidade | Precisa de calibração; reflexos e ventiladores geram ruído |
| LD2410C em vez do LD2420 (usado no começo) | LD2420 (alcance de até 8 m) | O LD2420 só informa presença e distância; o movimento tinha de ser deduzido pela variação da distância. O LD2410C separa movimento e parado com energia por faixa e é configurável por app (HLKRadarTool) | Alcance de ~6 m, suficiente para um cômodo. O firmware ainda aceita os dois (`RADAR_MODELO`) |
| **2 anéis WS2812B** | LED comum + transistor; fita de LED; lâmpada comercial | Um fio de dados por anel e qualquer cor: a mesma peça faz luz branca, luz noturna âmbar e o alerta piscando | Consomem até ~1 A no brilho máximo, por isso o brilho é limitado no código |
| **DHT22** | DHT11; BME280 | ±0,5 °C e faixa até 80 °C, suficiente para o alerta de calor; biblioteca estável | Lento (1 leitura a cada 2 s); fica longe do calor do LED |
| **MC-38 (reed switch)** | Sensor de vibração; sensor de corrente na tomada | Passivo, sem energia, barato e à prova de falsos disparos | Precisa de fio até o ESP32, por isso fica num nó separado na geladeira |
| Fonte 5 V ≥ 2 A | USB do computador | Wi-Fi + LEDs + radar causaram brownout no USB 2.0 | Mais um item na demonstração |
| Carcaça impressa em 3D com difusor | Luminária pronta | Coloca o radar no centro, entre os anéis, com plástico (não metal) na frente da antena | Tempo de impressão |

## 2. Software embarcado

| Decisão | Por quê |
|---|---|
| **Regras de alerta na borda (no ESP32)** | O alerta nível 1 (luz + buzzer) funciona **sem internet**. A resposta na casa é imediata e menos dados saem de casa |
| Distância suavizada **por tempo** (constante de 1,5 s) | A primeira média móvel era por iteração (~50 vezes por segundo) e quase não filtrava. Por tempo, o comportamento não depende da velocidade do loop |
| **Histerese** de 15 cm no estado "chão" | Perto do limiar, a zona ficava alternando entre "chão" e "sentado". Com histerese, só sai do "chão" 15 cm abaixo |
| Queda = parado no chão **depois** de ter havido movimento | Evita alerta para algo que sempre esteve parado; representa o "estava andando e caiu" |
| Evento `MOVIMENTO` no máximo a cada 15 s | O radar alterna movimento/parado muito rápido; sem o limite, a API recebia dezenas de eventos por minuto |
| Escalonamento em níveis (casa → família → rede de apoio) | Reduz falso alarme: o idoso cancela no botão verde antes de preocupar a família |
| Três funções separadas (sensor, luz principal, luz noturna) | A pessoa controla a luz; o sensor nunca desliga junto; a luz noturna não incomoda (só de madrugada e com a luz apagada) |
| Credenciais em `secrets.h` fora do Git | Requisito de entrega: configuração de exemplo **sem credenciais** |
| Timeout de 2 s no HTTP | Com a API fora, o loop não trava e as regras locais continuam |
| Heartbeat com RSSI | Distingue "offline" de "sem movimento" e ajuda a diagnosticar o sinal no local |

## 3. Rede e comunicação

| Decisão | Alternativas | Por que escolhemos | Evolução |
|---|---|---|---|
| **Wi-Fi da casa (802.11 b/g/n, 2,4 GHz)** | LoRaWAN; Zigbee; Bluetooth; 4G | Já existe na maioria das casas, não exige gateway nem mensalidade, e o ESP32 já tem o rádio. 2,4 GHz atravessa paredes melhor que 5 GHz | 4G Cat-1 / SMS como contingência |
| **HTTP + JSON (REST) no MVP** | MQTT; CoAP; WebSocket | É o mesmo contrato da API .NET (`POST /api/eventos`), fácil de testar com `curl`/navegador, passa por qualquer NAT ou firewall. O volume é baixo (~1,7 MB/dia) | **MQTT** com QoS 1 e *last will* quando houver muitos dispositivos |
| ESP32 sempre **inicia** a conexão | Servidor na lâmpada recebendo comandos | Funciona atrás do NAT da casa sem abrir portas no roteador, o que é mais seguro | MQTT para receber comandos (ex.: configurar tempos pelo app) |
| NTP para a hora | Relógio RTC | Grátis e preciso; a API usa a hora de chegada se faltar | — |
| Servidor de teste em Python (`api-mock`) | Depender só da API na nuvem | Permite testar e medir o fluxo de rede no laboratório, sem depender do deploy das outras disciplinas, com o mesmo contrato | — |
| VM Azure com IP fixo + NSG | Serviço gerenciado (PaaS) | Alinhado à disciplina de DevOps; controle das portas | Proxy reverso com HTTPS na 443 |

## 4. Por que a solução é CitizenTech

- **Problema social real e crescente:** envelhecimento da população e idosos morando sozinhos.
- **Tecnologia acessível:** componentes do ecossistema Arduino, sem mensalidade obrigatória e usando a internet que a casa já tem.
- **Privacidade por projeto:** radar em vez de câmera; só eventos saem de casa.
- **Escala pública:** a mesma telemetria permite a um programa municipal priorizar visitas por risco (agente de saúde / UBS).
