#!/usr/bin/env python3
"""
Tô Bem — servidor de teste de eventos (API mock)
=================================================
Recebe os eventos do ESP32 pela rede local no contrato do firmware (POST /api/eventos com JSON),
para testar e gerar evidências do fluxo de comunicação sem depender da nuvem.

Só usa a biblioteca padrão do Python 3.8+ (nada para instalar).

    python servidor_eventos.py               # porta 8080
    python servidor_eventos.py --porta 8081
    python servidor_eventos.py --ocultar-ip  # troca os IPs por x.x.x.x (para prints e vídeos)

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

Modo ponte para a API Java (o app da família lê os alertas de lá):

    python servidor_eventos.py --java http://localhost:8080
    python servidor_eventos.py --java http://IP-DA-VM:8080

Nesse modo o servidor sobe na porta 8090 (a 8080 fica para a API Java) e, além de mostrar e gravar
os eventos, repassa cada um para a API Java no formato dela (ver ponte_java.py). A conta (ADMIN ou
MANAGER) vem das variáveis JAVA_EMAIL e JAVA_SENHA. Sem JAVA_SENHA, o servidor pede a senha no terminal
(sem mostrar o que é digitado). Nenhuma senha fica no código.

Segurança (servidor de teste para a rede local, não para a internet)
  - corpo do POST limitado a 4 KB e campos validados (tipo, dispositivoId, valor, dataHora);
  - sem CORS: outras páginas abertas no navegador não conseguem ler os eventos;
  - a memória guarda só os últimos eventos (o CSV guarda todos).
"""
import argparse
import csv
import getpass
import json
import os
import socket
import threading
import re
import time
import sys
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))  # acha o ponte_java.py ao lado
from ponte_java import ErroJava, PonteJava  # noqa: E402
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
MAX_CORPO_BYTES = 4096        # um evento tem ~100 bytes; corpo maior é recusado (413)
MAX_EVENTOS_MEMORIA = 5000    # o painel e a API usam só os últimos; o CSV guarda todos
DATA_HORA_RE = re.compile(r"^[0-9T:.+\-Z ]{10,35}$")  # ISO 8601, ex.: 2026-11-19T21:29:45-03:00
IP_OCULTO = "x.x.x.x"

eventos = []          # memória: lista de dicts
dispositivos = {}     # id -> {"ultimo": ts, "rssi": int, "ip": str, "offline_avisado": bool}
trava = threading.Lock()
arquivo_csv = None
ponte = None          # PonteJava quando o servidor sobe com --java
ocultar_ip = False    # --ocultar-ip: troca os IPs por x.x.x.x na tela, no painel e no CSV
proximo_id = 1


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
    """Regras do contrato POST /api/eventos: 400 se faltar campo, tipo desconhecido etc."""
    if not isinstance(corpo, dict):
        return "o corpo precisa ser um objeto JSON"
    did = corpo.get("dispositivoId")
    if not isinstance(did, int) or isinstance(did, bool) or not 0 < did < 2**31:
        return "dispositivoId deve ser um inteiro positivo"
    if corpo.get("tipo") not in TIPOS_VALIDOS:
        return f"tipo inválido: {corpo.get('tipo')!r}"
    valor = corpo.get("valor", 1)
    if not isinstance(valor, (int, float)) or isinstance(valor, bool) or abs(valor) > 1e6:
        return "valor deve ser numérico"
    data_hora = corpo.get("dataHora")
    if data_hora is not None and (not isinstance(data_hora, str) or not DATA_HORA_RE.match(data_hora)):
        return "dataHora deve estar no formato ISO 8601"
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
<div class="sub">__SUBTITULO__ Atualiza a cada 2 s.</div>
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
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(corpo)

    def log_message(self, fmt, *args):  # silencia o log padrão; imprimimos o nosso
        pass

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == "/":
            if ponte:
                sub = "Ponte da rede local: recebe POST /api/eventos do ESP32 e repassa para a API Java."
            else:
                sub = "Servidor de teste da rede local: recebe POST /api/eventos do ESP32."
            corpo = PAINEL.replace("__SUBTITULO__", sub).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(corpo)))
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("X-Frame-Options", "DENY")
            self.end_headers()
            self.wfile.write(corpo)
        elif url.path == "/api/eventos":
            try:
                limite = min(max(int(parse_qs(url.query).get("limite", ["50"])[0]), 1), 500)
            except ValueError:
                limite = 50
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
        ip_origem = IP_OCULTO if ocultar_ip else self.client_address[0]
        try:
            tamanho = int(self.headers.get("Content-Length", 0))
        except ValueError:
            tamanho = -1
        if not 0 < tamanho <= MAX_CORPO_BYTES:
            print(f"[413] {ip_origem} -> corpo vazio ou maior que {MAX_CORPO_BYTES} bytes", flush=True)
            self.close_connection = True
            return self._json(413, {"erro": f"corpo deve ter entre 1 e {MAX_CORPO_BYTES} bytes"})
        try:
            corpo = json.loads(self.rfile.read(tamanho).decode("utf-8"))
        except (ValueError, UnicodeDecodeError):
            print(f"[400] {ip_origem} -> JSON inválido", flush=True)
            return self._json(400, {"erro": "JSON inválido"})
        erro = validar(corpo)
        if erro:
            print(f"[400] {ip_origem} -> {erro}", flush=True)
            return self._json(400, {"erro": erro})

        global proximo_id
        with trava:
            ev = {
                "id": proximo_id,
                "recebidoEm": datetime.now().isoformat(timespec="milliseconds"),
                "ipOrigem": ip_origem,
                "dispositivoId": corpo["dispositivoId"],
                "tipo": corpo["tipo"],
                "valor": corpo.get("valor", 1),
            }
            if corpo.get("dataHora"):
                ev["dataHora"] = corpo["dataHora"]
            proximo_id += 1
            eventos.append(ev)
            del eventos[:-MAX_EVENTOS_MEMORIA]
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
        if ponte:
            ponte.enviar(ev)   # repassa para a API Java em segundo plano


def main():
    global arquivo_csv, ponte, ocultar_ip
    ap = argparse.ArgumentParser(description="Servidor de teste de eventos do Tô Bem")
    ap.add_argument("--porta", type=int, default=None, help="padrão 8080 (8090 com --java)")
    ap.add_argument("--java", metavar="URL", help="repassa os eventos para a API Java, ex.: http://localhost:8080")
    padrao_csv = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "evidencias", "logs",
                              f"eventos_recebidos_{datetime.now():%Y%m%d}.csv")
    ap.add_argument("--csv", default=os.path.normpath(padrao_csv), help="arquivo CSV de saída")
    ap.add_argument("--ocultar-ip", action="store_true",
                    help="mostra x.x.x.x no lugar dos IPs (terminal, painel e CSV) — use para prints e vídeos")
    args = ap.parse_args()
    ocultar_ip = args.ocultar_ip
    porta = args.porta or (8090 if args.java else 8080)
    if args.java:
        email = os.environ.get("JAVA_EMAIL") or input("E-mail da conta ADMIN/MANAGER da API Java: ").strip()
        senha = os.environ.get("JAVA_SENHA") or getpass.getpass(f"Senha de {email} (não aparece ao digitar): ")
        ponte = PonteJava(args.java, email, senha)
        try:
            papel = ponte.login()
            print(f"[JAVA] conectado em {args.java} como {ponte.email} ({papel})", flush=True)
        except ErroJava as e:
            print(f"[JAVA] não consegui entrar na API Java: {e}", flush=True)
            print("[JAVA] confira se a API está no ar e defina JAVA_EMAIL / JAVA_SENHA. Vou tentar de novo a cada evento.",
                  flush=True)
        ponte.iniciar()
    arquivo_csv = args.csv
    os.makedirs(os.path.dirname(arquivo_csv), exist_ok=True)

    threading.Thread(target=vigiar_offline, daemon=True).start()
    ip = IP_OCULTO if ocultar_ip else ip_local()
    print("=" * 66)
    print(" Tô Bem — servidor de teste de eventos")
    if ocultar_ip:
        print(f" No secrets.h do ESP32:  #define SECRET_API_URL \"http://IP-DESTE-PC:{porta}/api/eventos\"")
        print("                         (IP oculto; veja com ipconfig)")
    else:
        print(f" No secrets.h do ESP32:  #define SECRET_API_URL \"http://{ip}:{porta}/api/eventos\"")
    print(f" Painel no navegador:    http://localhost:{porta}/")
    print(f" Gravando em:            {arquivo_csv}")
    if ponte:
        print(f" Repassando para:        API Java em {ponte.base} (linhas [JAVA])")
    print(" Windows: se o ESP32 não conseguir enviar, libere o Python no Firewall (rede privada).")
    print("=" * 66, flush=True)
    ThreadingHTTPServer(("0.0.0.0", porta), Handler).serve_forever()


if __name__ == "__main__":
    main()
