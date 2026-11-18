// =====================================================================
//  Tô Bem — configuração de exemplo (SEM credenciais reais)
//  1) Copie este arquivo para "secrets.h" na mesma pasta.
//  2) Preencha o secrets.h com o Wi-Fi e o endereço da API.
//  O secrets.h está no .gitignore e nunca vai para o GitHub.
// =====================================================================
#pragma once

#define SECRET_WIFI_SSID   "SUA_REDE"     // nome do Wi-Fi 2,4 GHz (o ESP32 não conecta em 5 GHz)
#define SECRET_WIFI_SENHA  "SUA_SENHA"

// Endereço da API que recebe os eventos. Vazio = só imprime no Serial.
//   Teste local (ferramentas/api-mock):  "http://192.168.0.10:8080/api/eventos"  (IP do seu PC)
//   API .NET na nuvem:                   "http://IP-PUBLICO-DA-VM:8080/api/eventos"
#define SECRET_API_URL     ""
