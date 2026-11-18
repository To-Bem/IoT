# Tô Bem — simulação no Wokwi

> **Atenção:** a simulação usa pinos diferentes da montagem física e o radar é simulado por chaves e um potenciômetro. Para o protótipo real, use `firmware/To-Bem_IoT/` e o circuito em [`docs/img/circuito-lampada.png`](../docs/img/circuito-lampada.png). As regras (queda, inatividade, luz noturna, escalonamento, botões) são as mesmas; o filtro da distância e o limite de eventos de movimento existem só no firmware físico.

Firmware da lâmpada Tô Bem (protótipo da GS 2026/2) rodando no simulador [Wokwi](https://wokwi.com), antes de as peças chegarem.

## Como abrir no Wokwi
1. Acesse **wokwi.com → New Project → ESP32** (escolha o modelo ESP32 genérico).
2. Na aba **`sketch.ino`**, apague tudo e cole o conteúdo do `sketch.ino` deste projeto.
3. Na aba **`diagram.json`**, apague tudo e cole o conteúdo do `diagram.json`.
4. Clique na seta ao lado das abas → **New file** → nome `libraries.txt` → cole o conteúdo do `libraries.txt`.
   (Ou use o **Library Manager** e adicione `Adafruit NeoPixel` e `DHT sensor library for ESPx`.)
5. Clique em **Play (▶)** e acompanhe o **Serial Monitor**: a linha `[STATUS]` a cada 3 s mostra tudo o que o sistema está "vendo".

Salve o projeto logado na sua conta para ter um link fixo e compartilhar com o grupo.

## O que cada peça representa

| Peça no Wokwi | Representa | Pino |
|---|---|---|
| Anel de 16 LEDs | Luz da lâmpada (principal, noturna e alertas) | GPIO 26 |
| Botão verde | "Estou bem" (base) | GPIO 18 |
| Botão azul | "Saí / Voltei" (base) | GPIO 19 |
| Botão branco | "Luz" — o botão sem fio da capa do interruptor | GPIO 27 |
| Buzzer | Aviso sonoro (base) | GPIO 25 |
| DHT22 | Temperatura/umidade (base). Clique nele para mudar a temperatura | GPIO 4 |
| `swGeladeira` (chave) | Reed switch da geladeira (o Wokwi não tem reed switch) | GPIO 33 |
| `swPresenca` (chave) | **Radar simulado:** tem alguém no cômodo | GPIO 32 |
| `swMovimento` (chave) | **Radar simulado:** a pessoa está se mexendo | GPIO 14 |
| `potDistancia` (potenciômetro) | **Radar simulado:** distância do teto até a pessoa (0–300 cm) | GPIO 34 |
| `swMadrugada` (chave) | Força o "modo madrugada" na demo (luz noturna) | GPIO 13 |

O **LD2410C não existe no Wokwi**, por isso é simulado pelas duas chaves e pelo potenciômetro. Passe o mouse sobre cada peça para ver o nome (id).
As chaves estão ligadas com `INPUT_PULLUP`: em uma posição a função fica **ativa**, na outra **inativa**. Na dúvida, mude a chave e confira a linha `[STATUS]` no Serial Monitor.

## As 3 funções separadas
- **Sensor (radar):** sempre ligado, independe da luz.
- **Luz principal:** só muda quando alguém aperta o botão branco "Luz". Nunca acende ou apaga sozinha.
- **Luz noturna:** âmbar fraca, acende sozinha só se a luz principal estiver apagada, for madrugada e houver movimento; apaga sozinha.

**Prioridade do anel:** alerta (amarelo/vermelho piscando) > confirmação de botão (verde/azul por 1 s) > luz principal (branca) > luz noturna (âmbar) > apagado.

## Roteiros de teste (viram evidência para a disciplina de IoT)
Os tempos estão **encurtados para demonstração** (ver constantes no topo do `sketch.ino`).

| # | Faça isto | Resultado esperado |
|---|---|---|
| T1 | Presença ON, Movimento ON | Evento `MOVIMENTO` no Serial |
| T2 | Aperte **Luz** | Anel branco; aperte de novo, apaga. O `[STATUS]` mostra que o radar continua lendo |
| T3 | Luz apagada, **Madrugada** ON, Movimento ON | Luz noturna âmbar por ~15 s |
| T4 | **Possível queda:** Movimento ON → depois Movimento OFF com Presença ON e distância **≥ 200 cm** (potenciômetro para o fim) | Após ~20 s: `ALERTA_QUEDA_N1`, anel amarelo piscando + buzzer |
| T5 | Durante T4, aperte o **verde** | Alerta resolvido, anel verde por 1 s, evento `BOTAO_OK` |
| T6 | Repita T4 e **não** aperte nada | Após ~15 s: `ALERTA_QUEDA_N2` (família), anel vermelho |
| T7 | **Sofá × chão:** Presença ON, Movimento OFF, distância **< 200 cm** | Nenhum alerta de queda (pessoa sentada) |
| T8 | **Cenário TV no escuro:** Luz apagada, pessoa no sofá (T7) → Movimento ON (levantou) → Movimento OFF com distância ≥ 200 cm | Alerta de queda mesmo com a luz apagada |
| T9 | Presença OFF por 60 s | `SEM_PRESENCA` + `ALERTA_INATIVIDADE_N1` |
| T10 | Aperte **azul** (saí) e deixe Presença OFF | Nenhum alerta de inatividade |
| T11 | Mude a chave da geladeira | Evento `GELADEIRA_ABERTA` |
| T12 | Clique no DHT22 e coloque 35 °C | Após ~10 s: `TEMPERATURA_ALTA` + 3 bipes |

## Enviar os eventos para a API .NET
No topo do `sketch.ino`, preencha `API_URL` com o endereço público da API, por exemplo `http://IP-DA-VM:8080/api/eventos`.
Com `API_URL` vazio, os eventos aparecem só no Serial Monitor. O Wokwi conecta na internet pela rede `Wokwi-GUEST`. A API precisa estar acessível publicamente (VM Azure), não em `localhost`.

Formato enviado (POST, JSON):
```json
{"dispositivoId":1,"tipo":"ALERTA_QUEDA_N1","valor":1.0,"dataHora":"2026-11-12T07:42:10-03:00"}
```

Tipos de evento que a API precisa aceitar:
`INICIADO`, `HEARTBEAT`, `MOVIMENTO`, `PRESENCA_PARADA`, `SEM_PRESENCA`, `GELADEIRA_ABERTA`, `BOTAO_OK`, `BOTAO_AUSENTE`, `BOTAO_VOLTEI`, `LUZ_LIGADA`, `LUZ_DESLIGADA`, `TEMPERATURA`, `TEMPERATURA_ALTA`, `ALERTA_QUEDA_N1`, `ALERTA_QUEDA_N2`, `ALERTA_INATIVIDADE_N1`, `ALERTA_INATIVIDADE_N2`, `ALERTA_RESOLVIDO_MOVIMENTO`.

## Passando para o hardware real
1. Instale a biblioteca **`ld2410`** (ncmreynolds) na IDE do Arduino.
2. No topo do `sketch.ino`, troque `USAR_RADAR_REAL` para `1`.
3. Troque `WIFI_SSID` e `WIFI_SENHA` pelos da rede usada na demo (não versionar a senha no GitHub).
4. As chaves `swPresenca`, `swMovimento`, `swMadrugada` e o potenciômetro deixam de ser necessários; o reed switch real entra no lugar de `swGeladeira` (mesmo pino 33).
5. O buzzer real é **ativo**: o `tone()` funciona, mas se o som ficar estranho troque por `digitalWrite`.

Ligações completas: ver o PDF *Guia de conexões do protótipo A*. O botão **Luz** (branco) usa a **via 8** do cabo de rede, no GPIO 27.

## Limitações da simulação
- O radar é simulado: a lógica de distância (sofá × chão) usa o potenciômetro. Com o LD2410C real, os limiares precisam ser calibrados.
- O Wokwi não tem reed switch; a chave deslizante faz o papel.
- O buzzer do Wokwi é passivo (toca frequências); o da lista de compras é ativo.
