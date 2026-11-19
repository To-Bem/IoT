"""
Tô Bem — ponte entre a lâmpada (ESP32) e a API Java
===================================================
Usada pelo servidor_eventos.py quando ele sobe com --java. Só biblioteca padrão do Python 3.8+.

Por que existe: o ESP32 manda um JSON simples para POST /api/eventos, sem login. A API Java
(repositório JAVA) espera outra coisa:

  ESP32 envia                              API Java espera
  ---------------------------------------  --------------------------------------------------------
  POST /api/eventos                        POST /api/dispositivos/{id}/evento
  sem token                                token JWT de ADMIN ou MANAGER (POST /api/auth/login)
  dataHora "2026-11-19T19:10:00-03:00"     dataHora "2026-11-19T19:10:00" (LocalDateTime, sem fuso)
  valor decimal (27.5)                     valor Integer
  HEARTBEAT como evento                    POST /api/dispositivos/{id}/heartbeat
  ALERTA_QUEDA_N2 só como evento           o alerta é criado à parte: POST /api/eventos/{id}/alerta

A ponte faz essa tradução no PC. Assim o firmware não guarda senha de administrador e não precisa
renovar token. As regras evento -> alerta são as mesmas do tool/simular.dart do repositório Mobile.
"""
import json
import queue
import re
import threading
import time
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime

# evento da lâmpada -> alerta que a API Java deve abrir (tipo, nível)
ALERTAS = {
    "ALERTA_QUEDA_N2": ("QUEDA", "CRITICO"),
    "ALERTA_INATIVIDADE_N2": ("INATIVIDADE", "ALERTA"),
    "TEMPERATURA_ALTA": ("TEMPERATURA_ALTA", "ATENCAO"),
}
# evento da lâmpada -> tipos de alerta abertos que ele encerra
RESOLVE = {
    "BOTAO_OK": {"QUEDA", "INATIVIDADE"},            # a pessoa apertou "Estou bem"
    "ALERTA_RESOLVIDO_MOVIMENTO": {"INATIVIDADE"},   # voltou a se mexer
}
RE_DATA = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}")


class ErroJava(Exception):
    def __init__(self, status, mensagem):
        super().__init__(f"HTTP {status}: {mensagem}")
        self.status = status


def _mensagem_de_erro(corpo):
    """Extrai a mensagem dos formatos de erro da API Java."""
    try:
        dados = json.loads(corpo)
    except ValueError:
        return corpo[:200] or "sem corpo"
    if isinstance(dados, dict):
        partes = [str(dados.get("message") or dados.get("error") or "")]
        for e in dados.get("errors") or []:
            if isinstance(e, dict):
                partes.append(f"{e.get('field')}: {e.get('message')}")
        return " | ".join(p for p in partes if p) or corpo[:200]
    return corpo[:200]


class PonteJava:
    def __init__(self, base_url, email, senha, intervalo_temperatura_s=300, timeout_s=10):
        self.base = base_url.rstrip("/")
        self.email, self.senha = email, senha
        self.intervalo_temp = intervalo_temperatura_s
        self.timeout = timeout_s
        self.token = None
        self.fila = queue.Queue()
        self.disp_cache = {}        # id -> DispositivoResponse
        self.online_ok = set()      # dispositivos já conferidos como ONLINE nesta execução
        self.ultima_temp = {}       # id -> time.time() do último TEMPERATURA repassado
        self.ultimo_hb_log = {}     # id -> time.time() do último log de heartbeat

    # ------------------------------------------------------------------ HTTP
    def _req(self, metodo, caminho, corpo=None, auth=True, tentar_login=True):
        dados = json.dumps(corpo).encode("utf-8") if corpo is not None else None
        req = urllib.request.Request(self.base + caminho, data=dados, method=metodo)
        req.add_header("Accept", "application/json")
        if dados is not None:
            req.add_header("Content-Type", "application/json")
        if auth:
            if not self.token:
                self.login()
            req.add_header("Authorization", f"Bearer {self.token}")
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                texto = resp.read().decode("utf-8")
                return json.loads(texto) if texto else None
        except urllib.error.HTTPError as e:
            texto = e.read().decode("utf-8", errors="replace")
            if e.code == 401 and auth and tentar_login:   # token vencido (dura 1 h): entra de novo
                self.token = None
                return self._req(metodo, caminho, corpo, auth, tentar_login=False)
            raise ErroJava(e.code, _mensagem_de_erro(texto)) from None
        except urllib.error.URLError as e:
            raise ErroJava(0, f"API Java fora do ar em {self.base} ({e.reason})") from None

    def login(self):
        resp = self._req("POST", "/api/auth/login", {"email": self.email, "password": self.senha}, auth=False)
        self.token = (resp or {}).get("token")
        if not self.token:
            raise ErroJava(0, "a API não devolveu o token")
        papel = (resp or {}).get("role")
        if papel not in ("ADMIN", "MANAGER"):
            print(f"[JAVA] ATENÇÃO: {self.email} é {papel}; a API só aceita eventos de ADMIN ou MANAGER", flush=True)
        return papel

    # ------------------------------------------------------------- fila
    def iniciar(self):
        threading.Thread(target=self._trabalhar, daemon=True).start()

    def enviar(self, ev):
        """Chamado pelo servidor depois de responder 201 ao ESP32 (o ESP32 não espera pela API Java)."""
        self.fila.put(ev)

    def _trabalhar(self):
        while True:
            ev = self.fila.get()
            try:
                self.tratar(ev)
            except ErroJava as e:
                print(f"[JAVA] ERRO disp {ev.get('dispositivoId')} {ev.get('tipo')}: {e}", flush=True)
            except Exception as e:  # nunca derruba a ponte
                print(f"[JAVA] ERRO inesperado em {ev.get('tipo')}: {e!r}", flush=True)

    # ------------------------------------------------------------- regras
    @staticmethod
    def data_hora(ev):
        """LocalDateTime sem fuso. Sem hora do ESP32 (NTP falhou), usa a hora deste PC."""
        dh = str(ev.get("dataHora") or "")
        if RE_DATA.match(dh):
            return dh[:19]
        return datetime.now().strftime("%Y-%m-%dT%H:%M:%S")

    def dispositivo(self, did):
        if did not in self.disp_cache:
            try:
                self.disp_cache[did] = self._req("GET", f"/api/dispositivos/{did}")
            except ErroJava as e:
                if e.status == 404:
                    raise ErroJava(404, f"dispositivo {did} não existe na API Java. Cadastre a lâmpada no app "
                                        f"e coloque o id dela em DISPOSITIVO_ID no firmware") from None
                raise
        return self.disp_cache[did]

    def garantir_online(self, did):
        """O app só considera a lâmpada online se o cadastro dela estiver como ONLINE."""
        if did in self.online_ok:
            return
        d = self.dispositivo(did)
        if d.get("status") != "ONLINE":
            self._req("PUT", f"/api/dispositivos/{did}",
                      {"tipo": d.get("tipo") or "LAMPADA", "comodo": d.get("comodo") or "", "status": "ONLINE"})
            d["status"] = "ONLINE"
            print(f"[JAVA] dispositivo {did} marcado como ONLINE", flush=True)
        self.online_ok.add(did)

    def heartbeat(self, did):
        self._req("POST", f"/api/dispositivos/{did}/heartbeat")
        agora = time.time()
        if agora - self.ultimo_hb_log.get(did, 0) > 60:   # não polui o terminal: 1 linha por minuto
            self.ultimo_hb_log[did] = agora
            print(f"[JAVA] heartbeat do dispositivo {did} ok", flush=True)

    def resolver(self, idoso_id, tipos):
        resolvidos = []
        for status in ("ABERTO", "ESCALONADO"):
            qs = urllib.parse.urlencode({"idosoId": idoso_id, "status": status, "size": 50})
            pagina = self._req("GET", f"/api/alertas/byOtherFilter?{qs}") or {}
            for a in pagina.get("content") or []:
                if a.get("tipo") in tipos:
                    self._req("PUT", f"/api/alertas/{a['id']}", {
                        "tipo": a["tipo"], "nivel": a["nivel"], "status": "RESOLVIDO",
                        "nivelEscalonamento": a.get("nivelEscalonamento") or 1})
                    resolvidos.append(f"{a['tipo']} #{a['id']}")
        return resolvidos

    def tratar(self, ev):
        did, tipo = int(ev["dispositivoId"]), ev["tipo"]
        self.garantir_online(did)

        if tipo == "HEARTBEAT":
            return self.heartbeat(did)
        if tipo == "INICIADO":
            self.heartbeat(did)
        if tipo == "TEMPERATURA":
            agora = time.time()
            if agora - self.ultima_temp.get(did, 0) < self.intervalo_temp:
                return                                   # 1 leitura a cada 5 min basta para o app
            self.ultima_temp[did] = agora

        valor = ev.get("valor", 1)
        evento = self._req("POST", f"/api/dispositivos/{did}/evento", {
            "tipo": tipo,
            "valor": int(round(float(valor))) if isinstance(valor, (int, float)) else 1,
            "dataHora": self.data_hora(ev),
        }) or {}
        linha = f"[JAVA] disp {did} {tipo:<26} -> evento #{evento.get('id')}"

        if tipo in ALERTAS:
            tipo_alerta, nivel = ALERTAS[tipo]
            alerta = self._req("POST", f"/api/eventos/{evento.get('id')}/alerta", {
                "tipo": tipo_alerta, "nivel": nivel, "status": "ABERTO", "nivelEscalonamento": 1}) or {}
            linha += f" + ALERTA {tipo_alerta} ({nivel}) #{alerta.get('id')} para o usuário {alerta.get('usuarioId')}"
        if tipo in RESOLVE:
            idoso_id = self.dispositivo(did).get("idosoId")
            feitos = self.resolver(idoso_id, RESOLVE[tipo]) if idoso_id else []
            if feitos:
                linha += " | resolvidos: " + ", ".join(feitos)
        print(linha, flush=True)
