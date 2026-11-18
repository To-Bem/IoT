#!/usr/bin/env python3
"""
Tô Bem — servidor de teste de eventos (API mock)
=================================================
Recebe os eventos do ESP32 pela rede local com o MESMO contrato da API .NET de telemetria
(POST /api/eventos), para testar e gerar evidências do fluxo de comunicação sem depender da nuvem.

Só usa a biblioteca padrão do Python 3.8+ (nada para instalar).

    python servidor_eventos.py               # porta 8080
    python servidor_eventos.py --porta 8081

Depois, no secrets.h do firmware:
    #define SECRET_API_URL "http://IP-DESTE-PC:8080/api/eventos"
(o IP aparece na tela quando o servidor sobe; PC e ESP32 precisam estar no mesmo Wi-Fi)

Rotas
  POST /api/eventos        recebe um evento JSON -> 201 (ok) | 400 (inválido)
  GET  /api/eventos        últimos eventos em JSON (?limite=50)
  GET  /api/dispositivos   situação de cada dispositivo (online/offline, último heartbeat, sinal Wi-Fi)
  GET  /                   painel no navegador, atualiza sozinho a cada 2 s

Cada evento recebido é gravado em CSV (evidencias/logs/eventos_recebidos_AAAAMMDD.csv),
com o horário de chegada e o IP de origem: é a evidência de que o dado atravessou a rede.
"""
import argparse
import csv
import json
import os
import socket
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

TIPOS_VALIDOS = {
    "INICIADO", "HEARTBEAT", "MOVIMENTO", "PRESENCA_PARADA", "SEM_PRESENCA",
    "GELADEIRA_ABERTA", "BOTAO_OK", "BOTAO_AUSENTE", "BOTAO_VOLTEI",
    "LUZ_LIGADA", "LUZ_DESLIGADA", "TEMPERATURA", "TEMPERATURA_ALTA",
    "ALERTA_QUEDA_N1", "ALERTA_QUEDA_N2", "ALERTA_INATIVIDADE_N1", "ALERTA_INATIVIDADE_N2",
    "ALERTA_RESOLVIDO_MOVIMENTO",
}
NOMES_DISPOSITIVOS = {1: "Lâmpada (sala)", 2: "Geladeira"}
OFFLINE_APOS_S = 30  # 3 heartbeats de 10 s sem notícia = offline (aviso técnico, não emergência)

eventos = []          # memória: lista de dicts
dispositivos = {}     # id -> {"ultimo": ts, "rssi": int, "ip": str, "offline_avisado": bool}
trava = threading.Lock()
arquivo_csv = None


def ip_local():
    """IP deste PC na rede local (o que vai no SECRET_API_URL)."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))  # não envia nada; só descobre a interface de saída
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def gravar_csv(ev):
    novo = not os.path.exists(arquivo_csv)
    with open(arquivo_csv, "a", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        if novo:
            w.writerow(["id", "recebidoEm", "ipOrigem", "dispositivoId", "tipo", "valor", "dataHoraDispositivo"])
        w.writerow([ev["id"], ev["recebidoEm"], ev["ipOrigem"], ev["dispositivoId"], ev["tipo"],
                    ev["valor"], ev.get("dataHora", "")])


def validar(corpo):
    """Mesmas regras combinadas com a API .NET: 400 se faltar campo, tipo desconhecido etc."""
    if not isinstance(corpo, dict):
        return "o corpo precisa ser um objeto JSON"
    if not isinstance(corpo.get("dispositivoId"), int) or corpo["dispositivoId"] <= 0:
        return "dispositivoId deve ser um inteiro positivo"
    if corpo.get("tipo") not in TIPOS_VALIDOS:
        return f"tipo inválido: {corpo.get('tipo')!r}"
    if not isinstance(corpo.get("valor", 1), (int, float)):
        return "valor deve ser numérico"
    return None


def situacao_dispositivos():
    agora = time.time()
    lista = []
    for did, d in sorted(dispositivos.items()):
        seg = agora - d["ultimo"]
        lista.append({
            "dispositivoId": did,
            "nome": NOMES_DISPOSITIVOS.get(did, f"Dispositivo {did}"),
            "status": "ONLINE" if seg <= OFFLINE_APOS_S else "OFFLINE",
            "segundosDesdeUltimoContato": round(seg, 1),
            "rssiDbm": d.get("rssi"),
            "ipOrigem": d.get("ip"),
        })
    return lista


def vigiar_offline():
    """R6: dispositivo sem heartbeat vira AVISO TÉCNICO (não é alarme de emergência)."""
    while True:
        time.sleep(2)
        with trava:
            for did, d in dispositivos.items():
                seg = time.time() - d["ultimo"]
                if seg > OFFLINE_APOS_S and not d["offline_avisado"]:
                    d["offline_avisado"] = True
                    print(f"\n!!! AVISO TÉCNICO: dispositivo {did} sem contato há {seg:.0f} s "
                          f"(offline — não é emergência)\n", flush=True)


PAINEL = """<!doctype html><html lang="pt-br"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Tô Bem — eventos recebidos</title>
<style>
 body{font-family:system-ui,sans-serif;margin:24px;background:#f6f7f9;color:#1d2433}
 h1{font-size:20px;margin:0 0 4px} .sub{color:#5b6475;margin-bottom:18px;font-size:13px}
 .devs{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:18px}
 .dev{background:#fff;border-radius:10px;padding:12px 16px;min-width:200px;box-shadow:0 1px 3px #0001}
 .ON{color:#1a7f37;font-weight:700} .OFF{color:#b42318;font-weight:700}
 table{border-collapse:collapse;width:100%;background:#fff;border-radius:10px;overflow:hidden;box-shadow:0 1px 3px #0001;font-size:13px}
 th,td{padding:7px 10px;text-align:left;border-bottom:1px solid #eef0f3} th{background:#eef0f3}
 tr.alerta td{background:#fdecec} tr.resolvido td{background:#e9f7ee}
</style></head><body>
<h1>Tô Bem — eventos recebidos pela rede</h1>
<div class="sub">Servidor de teste (mesmo contrato da API .NET: POST /api/eventos). Atualiza a cada 2 s.</div>
<div class="devs" id="devs"></div>
<table><thead><tr><th>#</th><th>Recebido em</th><th>Dispositivo</th><th>Tipo</th><th>Valor</th><th>IP de origem</th></tr></thead>
<tbody id="ev"></tbody></table>
<script>
async function atualizar(){
  try{
    const d = await (await fetch('/api/dispositivos')).json();
    document.getElementById('devs').innerHTML = d.map(x =>
      `<div class="dev"><b>${x.nome}</b> (id ${x.dispositivoId})<br>
       <span class="${x.status=='ONLINE'?'ON':'OFF'}">${x.status}</span> · último contato há ${x.segundosDesdeUltimoContato}s<br>
       Wi-Fi: ${x.rssiDbm ?? '-'} dBm · ${x.ipOrigem}</div>`).join('') || '<i>Nenhum dispositivo ainda.</i>';
    const e = await (await fetch('/api/eventos?limite=40')).json();
    document.getElementById('ev').innerHTML = e.reverse().map(x =>
      `<tr class="${x.tipo.startsWith('ALERTA_RESOLVIDO')||x.tipo=='BOTAO_OK'?'resolvido':(x.tipo.startsWith('ALERTA')||x.tipo=='TEMPERATURA_ALTA'?'alerta':'')}">
       <td>${x.id}</td><td>${x.recebidoEm}</td><td>${x.dispositivoId}</td><td>${x.tipo}</td><td>${x.valor}</td><td>${x.ipOrigem}</td></tr>`).join('');
  }catch(err){}
}
atualizar(); setInterval(atualizar, 2000);
</script></body></html>"""


class Handler(BaseHTTPRequestHandler):
    server_version = "ToBemMock/1.0"

    def _json(self, status, obj):
        corpo = json.dumps(obj, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(corpo)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(corpo)

    def log_message(self, fmt, *args):  # silencia o log padrão; imprimimos o nosso
        pass

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/":
            corpo = PAINEL.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(corpo)))
            self.end_headers()
            self.wfile.write(corpo)
        elif url.path == "/api/eventos":
            limite = int(parse_qs(url.query).get("limite", ["50"])[0])
            with trava:
                self._json(200, eventos[-limite:])
        elif url.path == "/api/dispositivos":
            with trava:
                self._json(200, situacao_dispositivos())
        else:
            self._json(404, {"erro": "rota não encontrada"})

    def do_POST(self):
        if urlparse(self.path).path != "/api/eventos":
            return self._json(404, {"erro": "rota não encontrada"})
        try:
            tamanho = int(self.headers.get("Content-Length", 0))
            corpo = json.loads(self.rfile.read(tamanho).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            print(f"[400] {self.client_address[0]} -> JSON inválido", flush=True)
            return self._json(400, {"erro": "JSON inválido"})
        erro = validar(corpo)
        if erro:
            print(f"[400] {self.client_address[0]} -> {erro}", flush=True)
            return self._json(400, {"erro": erro})

        with trava:
            ev = {
                "id": len(eventos) + 1,
                "recebidoEm": datetime.now().isoformat(timespec="milliseconds"),
                "ipOrigem": self.client_address[0],
                "dispositivoId": corpo["dispositivoId"],
                "tipo": corpo["tipo"],
                "valor": corpo.get("valor", 1),
            }
            if corpo.get("dataHora"):
                ev["dataHora"] = corpo["dataHora"]
            eventos.append(ev)
            d = dispositivos.setdefault(ev["dispositivoId"], {"offline_avisado": False})
            if d.get("offline_avisado"):
                print(f"\n=== dispositivo {ev['dispositivoId']} voltou a falar (online)\n", flush=True)
            d.update(ultimo=time.time(), ip=ev["ipOrigem"], offline_avisado=False)
            if ev["tipo"] == "HEARTBEAT" and ev["valor"]:
                d["rssi"] = ev["valor"]
            gravar_csv(ev)

        destaque = "  <<< ALERTA" if ev["tipo"].startswith("ALERTA_") and "RESOLVIDO" not in ev["tipo"] else ""
        print(f"[201] {ev['recebidoEm'][11:23]}  disp {ev['dispositivoId']}  {ev['tipo']:<26} "
              f"valor={ev['valor']:<7} de {ev['ipOrigem']}{destaque}", flush=True)
        self._json(201, ev)


def main():
    global arquivo_csv
    ap = argparse.ArgumentParser(description="Servidor de teste de eventos do Tô Bem")
    ap.add_argument("--porta", type=int, default=8080)
    padrao_csv = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "evidencias", "logs",
                              f"eventos_recebidos_{datetime.now():%Y%m%d}.csv")
    ap.add_argument("--csv", default=os.path.normpath(padrao_csv), help="arquivo CSV de saída")
    args = ap.parse_args()
    arquivo_csv = args.csv
    os.makedirs(os.path.dirname(arquivo_csv), exist_ok=True)

    threading.Thread(target=vigiar_offline, daemon=True).start()
    ip = ip_local()
    print("=" * 66)
    print(" Tô Bem — servidor de teste de eventos")
    print(f" No secrets.h do ESP32:  #define SECRET_API_URL \"http://{ip}:{args.porta}/api/eventos\"")
    print(f" Painel no navegador:    http://localhost:{args.porta}/")
    print(f" Gravando em:            {arquivo_csv}")
    print(" Windows: se o ESP32 não conseguir enviar, libere o Python no Firewall (rede privada).")
    print("=" * 66, flush=True)
    ThreadingHTTPServer(("0.0.0.0", args.porta), Handler).serve_forever()


if __name__ == "__main__":
    main()
