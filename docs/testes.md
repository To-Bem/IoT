# Testes, evidências e métricas — Tô Bem

Este documento atende ao requisito **05: testes, evidências e demonstração do protótipo**. Ele traz o ambiente de teste, os critérios e métricas, os casos de teste com os resultados, o histórico de problemas encontrados e a análise das limitações.

**Legenda do resultado:** ✅ passou · ⚠️ passou com ressalva · ❌ falhou · ⬜ a executar

---

## 1. Ambiente de teste

| Item | Configuração |
|---|---|
| Protótipo físico | Painel com ESP32 DevKit V1, 2 anéis WS2812B (16 LEDs), radar HLK-LD2410C e DHT22 (circuito em [`img/circuito-lampada.png`](img/circuito-lampada.png)) |
| Firmware | `firmware/To-Bem_IoT` (tempos de demonstração: queda 20 s, resposta 15 s, inatividade 60 s) |
| Alimentação | Porta USB 3.0 do PC ou carregador 5 V 2 A |
| Posição do radar | _anotar: altura da lâmpada até o chão (cm) e se está no teto ou apontada para cima na bancada_ |
| Rede | Wi-Fi 2,4 GHz da casa ou do laboratório; servidor de teste no PC do grupo (`ferramentas/api-mock`) ou API .NET na VM |
| Simulação | Wokwi (`simulacao-wokwi/`): radar simulado por chaves e potenciômetro; base completa |
| Coleta | Monitor Serial a 115200 (uma linha `[STATUS]` por segundo) + CSV do servidor de teste + fotos e vídeo |

---

## 2. Critérios de aceite e métricas

| Métrica | Como medir | Critério de aceite |
|---|---|---|
| Taxa de detecção de presença | Entrar no campo do radar N = 10 vezes | ≥ 9/10 com `presença=1` em até 2 s |
| Detecção de queda simulada | Deitar embaixo da lâmpada e ficar imóvel, N = 5 | 5/5 `ALERTA_QUEDA_N1` em até `TEMPO_QUEDA` + 5 s |
| Falso alerta de queda | Pessoa sentada/em pé parada por 2 min, N = 3 | 0 alertas |
| Estabilidade da distância (pessoa parada) | Desvio padrão de `dist` no `[STATUS]` em 30 s (`analisar_log.py`) | ≤ 5 cm |
| Latência do envio | `API respondeu ... em N ms` (`analisar_log.py`) | Rede local: mediana ≤ 300 ms. Nuvem: mediana ≤ 1000 ms |
| Taxa de entrega | Respostas 2xx ÷ envios | ≥ 95% com Wi-Fi estável |
| Detecção de dispositivo offline | Desligar o ESP32 e cronometrar o aviso no servidor | Aviso técnico em ≤ 35 s (3 heartbeats de 10 s) |
| Estabilidade de energia | Reinícios (`rst:` / `Brownout`) no log em 30 min | 0 |
| Sinal Wi-Fi | RSSI no heartbeat | ≥ -75 dBm no local de instalação |

Gere as métricas colando o log do Monitor Serial num `.txt` e rodando `python ferramentas/analisar_log.py arquivo.txt`. A saída já vem em tabela Markdown.

---

## 3. Casos de teste

### H · Montagem e bring-up (protótipo físico)

| ID | Objetivo | Procedimento | Esperado | Resultado | Evidência |
|---|---|---|---|---|---|
| H1 | DHT22 | `ToBem_Diagnostico` | Temperatura e umidade válidas | ✅ 28,8 °C e 61% | log do diagnóstico |
| H2 | Anéis de LED | Animação de partida do firmware | 32 LEDs acendem em azul, um a um | ✅ depois de mudar os dados de D34/D35 (só entrada) para D33/D25 | foto / vídeo |
| H3 | UART do radar | `ToBem_Diagnostico` (procura pinos e baud) | Bytes com cabeçalho `F4 F3` a 256000 bps | ✅ depois de refazer a solda: antes, 0 bytes em todas as combinações e TX a 0 V no multímetro | log + foto do multímetro |
| H4 | Alimentação | Ligar o sistema completo com Wi-Fi | Sem reinícios | ⚠️ USB 2.0: reinício em loop (brownout). USB 3.0: estável | log do brownout + log estável |
| H5 | Carregador 5 V 2 A | Mesmo teste do H4 no carregador | Sem reinícios por 30 min | ⬜ | log |

### F · Funcionais (protótipo físico)

| ID | Objetivo | Procedimento | Esperado | Resultado | Evidência |
|---|---|---|---|---|---|
| F1 | Presença e movimento | Entrar e andar no campo do radar (N = 10) | `presença=1 mov=1`; evento `MOVIMENTO` | ⬜ _x/10_ | log |
| F2 | Limite de eventos | Movimentar-se sem parar por 1 min | No máximo 1 `MOVIMENTO` a cada 15 s | ✅ antes, eventos repetidos a cada leitura; depois do limite, nenhum repetido | log antes/depois |
| F3 | Leitura da distância parado | Pessoa sentada ao lado do painel virado para cima, imóvel | Distância estável | ⚠️ bruto: 72–77 cm parado (energia 100); em movimento oscila de 30 a 116 cm. Motivou o filtro por tempo (1,5 s) e a histerese | log `DEBUG_RADAR` |
| F4 | Calibração do "chão" | 30 s sentado + 30 s deitado; `analisar_log.py --calibrar` | Diferença clara entre as médias | ⬜ _sentado: __ cm · deitado: __ cm · limiar: __ cm_ | 2 logs + saída do script |
| F5 | Queda simulada | Movimento → deitar no "chão" imóvel (N = 5) | `PRESENCA_PARADA` + `ALERTA_QUEDA_N1` em ~20 s; anéis âmbar piscando | ⬜ _x/5, tempo médio __ s_ | vídeo + log |
| F6 | Escalonamento | Durante F5, não fazer nada | `ALERTA_QUEDA_N2` após 15 s; anéis vermelhos | ⬜ | vídeo + log |
| F7 | Sem falso alerta | Sentado/em pé parado por 2 min (N = 3) | Zona "sentado/em pé", nenhum alerta | ⬜ | log |
| F8 | Inatividade | Sair do campo do radar por 60 s | `SEM_PRESENCA` + `ALERTA_INATIVIDADE_N1` | ⬜ | log |
| F9 | Resolver por movimento | Durante F8, voltar ao cômodo | `ALERTA_RESOLVIDO_MOVIMENTO`, anel verde | ⬜ | log |
| F10 | Luz noturna | `DEMO_MADRUGADA = true`, luz apagada, mover a mão | Âmbar fraco por 15 s e apaga sozinho | ⬜ | vídeo |
| F11 | Calor | Aquecer o DHT22 (secador ou mão) acima de 32 °C | `TEMPERATURA_ALTA` após 10 s | ⬜ | log |
| F12 | Radar × PIR *(opcional)* | Pessoa imóvel na frente dos dois | PIR perde a pessoa; radar mantém `presença=1` | ⬜ | vídeo |

### W · Base e regras completas (Wokwi)

Roteiro detalhado em [`simulacao-wokwi/README.md`](../simulacao-wokwi/README.md).

| ID | Objetivo | Esperado | Resultado |
|---|---|---|---|
| W1 | Luz principal pelo botão branco | Anel branco liga/desliga; o radar continua lendo | ⬜ |
| W2 | Botão verde cancela o nível 1 | `BOTAO_OK`, anel verde, alerta resolvido | ⬜ |
| W3 | Botão azul "Saí" | Sem presença por 60 s **não** gera inatividade | ⬜ |
| W4 | Geladeira | Chave da geladeira → `GELADEIRA_ABERTA` | ⬜ |
| W5 | Sofá × chão | Parado com distância < limiar: nenhum alerta | ⬜ |
| W6 | Calor | DHT22 em 35 °C → `TEMPERATURA_ALTA` + 3 bipes | ⬜ |

### R · Rede (protótipo físico + servidor de teste ou API .NET)

| ID | Objetivo | Procedimento | Esperado | Resultado | Evidência |
|---|---|---|---|---|---|
| R1 | Conexão Wi-Fi | Ligar o ESP32 com o `secrets.h` preenchido | `Wi-Fi conectado \| IP ... \| RSSI ...` | ⬜ _IP: __, RSSI: __ dBm_ | log |
| R2 | Evento atravessa a rede | `api-mock` no PC; provocar F1 | `API respondeu 201`; linha no painel e no CSV com o IP do ESP32 | ⬜ | print do painel + CSV |
| R3 | Latência e entrega | 5 min de uso normal; `analisar_log.py` | Mediana ≤ 300 ms (LAN); ≥ 95% de respostas 2xx | ⬜ _mediana __ ms · p95 __ ms · __%_ | saída do script |
| R4 | Validação do contrato | `curl` com tipo inválido no `api-mock` (abaixo) | 400 com `{"erro": ...}` | ✅ testado no servidor de teste | saída do curl |
| R5 | Dispositivo offline | Desligar o ESP32 da energia | Painel marca OFFLINE e o terminal mostra AVISO TÉCNICO em ≤ 35 s | ⬜ _tempo: __ s_ | print |
| R6 | API fora do ar | Fechar o servidor de teste e provocar F5 | Alerta local (luz + buzzer) acontece mesmo assim; `API respondeu -1` sem travar o loop | ⬜ | vídeo + log |
| R7 | Wi-Fi fora do ar | Desligar o roteador ou mudar a senha no `secrets.h` | "Sem Wi-Fi: funcionando só localmente"; regras locais funcionando | ⬜ | log |
| R8 | Segundo dispositivo | Ligar o nó da geladeira e abrir a "porta" | `GELADEIRA_ABERTA` com `dispositivoId 2` no painel | ⬜ | print |
| R9 | API .NET real | Trocar a URL para a VM | 201 e evento no Oracle | ⬜ | print da API + tabela EVENTO |

```bash
# R4 — validação do contrato (com o servidor de teste rodando)
curl -i -X POST http://localhost:8080/api/eventos -H "Content-Type: application/json" -d "{\"dispositivoId\":1,\"tipo\":\"XYZ\"}"
# esperado: HTTP/1.0 400 Bad Request  {"erro": "tipo inválido: 'XYZ'"}
```

---

## 4. Problemas encontrados e soluções

Este histórico também é evidência: mostra o protótipo sendo depurado com medição, e não por tentativa.

| # | Sintoma | Diagnóstico | Solução |
|---|---|---|---|
| 1 | Anéis de LED não acendiam | Os dados estavam em GPIO 34 e 35, que no ESP32 são **só de entrada** | Fios mudados para D33 e D25 |
| 2 | Radar mandava 0 bytes em todas as combinações de pino e baud | Multímetro: TX e OUT do radar a 0 V, ou seja, sem alimentação/GND | Solda do VCC/GND refeita; a UART passou a responder |
| 3 | Pinos do radar diferentes do planejado | TX no D21, RX no D19, OUT no D18 | O código foi ajustado à montagem (sem ressoldar) |
| 4 | Reinício em loop em casa (funcionava no laboratório) | `Brownout detector was triggered`: Wi-Fi + LEDs + radar puxam mais do que a porta USB 2.0 fornece | Porta USB 3.0 / carregador 5 V 2 A; potência do Wi-Fi em 11 dBm; brilho limitado |
| 5 | `MOVIMENTO` repetido dezenas de vezes por minuto | O radar alterna "movendo/parado" muito rápido | Evento só na borda de subida e no máximo 1 a cada 15 s |
| 6 | Distância pulando (80 → 69 → 47 cm) com a pessoa parada | A média móvel era por iteração (~50 vezes por segundo), então quase não filtrava | Filtro por **tempo** (constante de 1,5 s) + histerese de 15 cm no "chão" |
| 7 | Erro "bootloader.bin foi inesperado" ao compilar no Windows | Caminho da pasta com parênteses | Projeto movido para `C:\Develop\To-Bem\` |

---

## 5. Métricas coletadas

_Cole aqui as tabelas geradas pelo `ferramentas/analisar_log.py` para os testes F3/F4 (distância), R3 (latência e entrega) e H5 (reinícios)._

| Métrica | Valor medido | Critério | Atende? |
|---|---|---|---|
| Detecção de presença (F1) | __/10 | ≥ 9/10 | ⬜ |
| Detecção de queda (F5) | __/5 · __ s | 5/5 | ⬜ |
| Falsos alertas de queda (F7) | __ | 0 | ⬜ |
| Desvio da distância parado, filtrada (F4) | __ cm | ≤ 5 cm | ⬜ |
| Latência mediana / p95 (R3) | __ / __ ms | ≤ 300 ms (LAN) | ⬜ |
| Taxa de entrega (R3) | __% | ≥ 95% | ⬜ |
| Tempo até o aviso de offline (R5) | __ s | ≤ 35 s | ⬜ |
| Reinícios em 30 min (H5) | __ | 0 | ⬜ |
| RSSI no local (R1) | __ dBm | ≥ -75 dBm | ⬜ |

---

## 6. Análise das limitações

| Limitação | Impacto | Mitigação atual | Próximo passo |
|---|---|---|---|
| A distância do radar oscila com movimento e reflexos | Pode confundir "sentado" com "chão" perto do limiar | Filtro por tempo, histerese, regra "parado depois de movimento" | Calibração por ambiente no app; sensibilidade por gate no HLKRadarTool |
| Calibração da maquete ≠ casa real | Os valores do protótipo não valem para um teto de 2,6 m | `LIMIAR_CHAO_CM` configurável | Assistente de calibração na instalação |
| Movimento falso (ventilador, cortina, pet) | Presença falsa ou alerta de inatividade não disparado | Gates e sensibilidade ajustáveis no radar | Combinar com a rotina aprendida (IoB) |
| Radar não detecta objetos imóveis sem respiração | Um boneco não serve para testar queda | Testes com pessoa real | — |
| Tempos de demonstração curtos | Gerariam muitos falsos alertas em uso real | Constantes separadas demo × produto | Tempos por idoso, vindos da API |
| HTTP sem TLS e sem fila offline | Evento perdido se o Wi-Fi cair; tráfego legível na internet | Alerta local independe da rede; heartbeat detecta offline | HTTPS + token, fila na flash, MQTT |
| Botões, buzzer e reed não montados no físico | Base validada só no simulador | Lógica idêntica no Wokwi | Base sem fio com ESP-NOW |
| Uma lâmpada cobre um cômodo | Queda em outro cômodo não é vista | — | Uma lâmpada por cômodo (banheiro e quarto primeiro) |

---

## 7. Como registrar as evidências

1. **Log:** no Monitor Serial, `Ctrl+A` e `Ctrl+C`, cole em `evidencias/logs/<ID>_<descricao>.txt` (ex.: `F5_queda.txt`).
2. **Rede:** o servidor de teste grava sozinho `evidencias/logs/eventos_recebidos_AAAAMMDD.csv`. Tire um print do painel `http://localhost:8080/`.
3. **Fotos e vídeos:** `evidencias/fotos/<ID>_<descricao>.jpg`. Vídeos longos vão no YouTube (não listado) ou no Drive, com o link em `evidencias/README.md`.
4. **Resultado:** troque o ⬜ do caso por ✅/⚠️/❌ e escreva o valor medido.
