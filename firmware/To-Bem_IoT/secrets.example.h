// =====================================================================
//  Tô Bem — configuração de exemplo (SEM credenciais reais)
//  1) Copie este arquivo para "secrets.h" na mesma pasta.
//  2) Preencha o secrets.h com o Wi-Fi e o endereço que recebe os eventos.
//  O secrets.h está no .gitignore e nunca vai para o GitHub.
// =====================================================================
#pragma once

#define SECRET_WIFI_SSID   "SUA_REDE"     // nome do Wi-Fi 2,4 GHz (o ESP32 não conecta em 5 GHz)
#define SECRET_WIFI_SENHA  "SUA_SENHA"

// Endereço que recebe os eventos. Vazio = só imprime no Serial.
//   Com a API Java (app da família):  "http://IP-DO-PC:8090/api/eventos"
//       -> no PC: python ferramentas/api-mock/servidor_eventos.py --java http://localhost:8080
//   Só o servidor de teste:           "http://IP-DO-PC:8080/api/eventos"
//       -> no PC: python ferramentas/api-mock/servidor_eventos.py
// O IP do PC aparece na tela quando o servidor sobe. PC e ESP32 no mesmo Wi-Fi.
#define SECRET_API_URL     ""
