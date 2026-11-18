#!/usr/bin/env python3
"""
Tô Bem — análise do log do Monitor Serial (métricas para docs/testes.md)
=======================================================================
Só usa a biblioteca padrão do Python 3.8+.

1) Métricas de um teste
   Copie o texto do Monitor Serial (Ctrl+A, Ctrl+C) para um .txt em evidencias/logs/ e rode:
       python analisar_log.py ../evidencias/logs/teste_T1.txt
   Sai uma tabela em Markdown (eventos por tipo, latência da API, sinal Wi-Fi, distância do radar)
   pronta para colar no docs/testes.md.

2) Calibração do limiar do "chão" (LIMIAR_CHAO_CM)
   Com DEBUG_RADAR = true, grave ~30 s com a pessoa sentada/em pé e ~30 s deitada, parada:
       python analisar_log.py --calibrar sentado.txt deitado.txt
   O script calcula a distância média parada de cada situação e sugere o limiar (ponto médio).
"""
import argparse
import re
import statistics as st
import sys
from collections import Counter

RE_RADAR = re.compile(r"\[RADAR\] mov=(\d) dist=(\d+)cm energia=(\d+) \| parado=(\d) dist=(\d+)cm energia=(\d+)")
RE_STATUS = re.compile(r"\[STATUS\].*presença=(\d) mov=(\d) dist=(\d+)cm \(([^)]*)\).*?°C \| alerta=(\S+)"
                       r"(?:.*wifi=(-?\d+) dBm)?")
RE_EVENTO = re.compile(r'\[EVENTO\].*"tipo":"([A-Z_0-9]+)".*"valor":(-?[\d.]+)')
RE_API = re.compile(r"API respondeu (-?\d+)(?: em (\d+) ms)?")
RE_RESET = re.compile(r"rst:0x|Brownout", re.I)


def resumo(nums):
    if not nums:
        return "—"
    s = sorted(nums)
    p95 = s[min(len(s) - 1, int(round(0.95 * (len(s) - 1))))]
    dp = st.pstdev(nums) if len(nums) > 1 else 0
    return (f"n={len(nums)} · média {st.mean(nums):.1f} · desvio {dp:.1f} · "
            f"mín {min(nums)} · mediana {st.median(nums):g} · p95 {p95} · máx {max(nums)}")


def ler(caminho):
    with open(caminho, encoding="utf-8", errors="replace") as f:
        return f.read().splitlines()


def analisar(linhas):
    m = {"parado": [], "mov": [], "energia_parado": [], "status": 0, "presenca": 0, "zonas": Counter(),
         "eventos": Counter(), "api_codigos": Counter(), "latencias": [], "rssi": [], "resets": 0,
         "dist_status": []}
    for l in linhas:
        if RE_RESET.search(l):
            m["resets"] += 1
        r = RE_RADAR.search(l)
        if r:
            mov, dmov, _, parado, dpar, epar = map(int, r.groups())
            if parado and not mov:
                m["parado"].append(dpar)
                m["energia_parado"].append(epar)
            if mov:
                m["mov"].append(dmov)
            continue
        s = RE_STATUS.search(l)
        if s:
            m["status"] += 1
            pres, mov, dist, zona, _, wifi = s.groups()
            if pres == "1":
                m["presenca"] += 1
                if mov == "0":
                    m["dist_status"].append(int(dist))
            m["zonas"][zona] += 1
            if wifi not in (None, "0"):
                m["rssi"].append(int(wifi))
            continue
        e = RE_EVENTO.search(l)
        if e:
            m["eventos"][e.group(1)] += 1
            if e.group(1) == "HEARTBEAT" and float(e.group(2)) < 0:
                m["rssi"].append(int(float(e.group(2))))
            continue
        a = RE_API.search(l)
        if a:
            m["api_codigos"][int(a.group(1))] += 1
            if a.group(2):
                m["latencias"].append(int(a.group(2)))
    return m


def relatorio(nome, m):
    out = [f"### Métricas — `{nome}`", "", "| Métrica | Valor |", "|---|---|"]
    out.append(f"| Linhas de status (≈ segundos de teste) | {m['status']} |")
    if m["status"]:
        out.append(f"| Tempo com presença detectada | {100 * m['presenca'] / m['status']:.0f}% |")
        out.append("| Zona (status) | " + ", ".join(f"{z}: {n}" for z, n in m["zonas"].most_common()) + " |")
    out.append(f"| Reinícios / brownout no log | {m['resets']} |")
    total_api = sum(m["api_codigos"].values())
    if total_api:
        ok = sum(n for c, n in m["api_codigos"].items() if 200 <= c < 300)
        out.append(f"| Envios para a API | {total_api} (sucesso 2xx: {ok}, {100 * ok / total_api:.0f}%) |")
        out.append("| Códigos HTTP | " + ", ".join(f"{c}: {n}" for c, n in sorted(m["api_codigos"].items())) + " |")
        out.append(f"| Latência POST → resposta (ms) | {resumo(m['latencias'])} |")
    else:
        out.append("| Envios para a API | nenhum (API_URL vazio ou sem Wi-Fi) |")
    out.append(f"| Sinal Wi-Fi (dBm) | {resumo(m['rssi'])} |")
    out.append(f"| Distância parado — radar bruto (cm) | {resumo(m['parado'])} |")
    out.append(f"| Distância em movimento — radar bruto (cm) | {resumo(m['mov'])} |")
    out.append(f"| Distância parado — filtrada no [STATUS] (cm) | {resumo(m['dist_status'])} |")
    out += ["", "| Evento | Quantidade |", "|---|---|"]
    out += [f"| {t} | {n} |" for t, n in sorted(m["eventos"].items())] or ["| — | 0 |"]
    return "\n".join(out)


def calibrar(arq_alto, arq_chao, histerese=15):
    a, c = analisar(ler(arq_alto)), analisar(ler(arq_chao))
    # usa o radar bruto (DEBUG_RADAR = true); se não houver, usa a distância filtrada do [STATUS]
    da = a["parado"] or a["dist_status"]
    dc = c["parado"] or c["dist_status"]
    if len(da) < 5 or len(dc) < 5:
        sys.exit("Poucas leituras paradas. Ligue DEBUG_RADAR = true e grave ~30 s de cada situação, sem se mexer.")
    ma, mc = st.mean(da), st.mean(dc)
    sa, sc = st.pstdev(da), st.pstdev(dc)
    limiar = round((ma + mc) / 2)
    folga = abs(mc - ma) / 2
    print("### Calibração do LIMIAR_CHAO_CM\n")
    print("| Situação | Leituras | Média (cm) | Desvio (cm) | Mín–máx |\n|---|---|---|---|---|")
    print(f"| Sentado/em pé (`{arq_alto}`) | {len(da)} | {ma:.1f} | {sa:.1f} | {min(da)}–{max(da)} |")
    print(f"| Deitado (`{arq_chao}`) | {len(dc)} | {mc:.1f} | {sc:.1f} | {min(dc)}–{max(dc)} |")
    print(f"\n**Sugestão:** `const int LIMIAR_CHAO_CM = {limiar};`  (ponto médio; folga de {folga:.0f} cm para cada lado)")
    if mc <= ma:
        print("\nATENÇÃO: deitado deu MENOR ou igual a sentado. O radar precisa estar acima da pessoa, apontando para baixo.")
    elif folga < histerese + 2 * max(sa, sc):
        print(f"\nATENÇÃO: separação pequena (folga {folga:.0f} cm < histerese {histerese} + 2 desvios). "
              "Aumente a altura da lâmpada, ajuste a sensibilidade no HLKRadarTool ou reduza HISTERESE_CM.")
    else:
        print("\nSeparação boa: a histerese de 15 cm cabe com margem.")


def main():
    ap = argparse.ArgumentParser(description="Métricas do log Serial do Tô Bem")
    ap.add_argument("logs", nargs="*", help="arquivos .txt com o texto do Monitor Serial")
    ap.add_argument("--calibrar", nargs=2, metavar=("SENTADO", "DEITADO"))
    args = ap.parse_args()
    if args.calibrar:
        calibrar(*args.calibrar)
    elif args.logs:
        for arq in args.logs:
            print(relatorio(arq, analisar(ler(arq))) + "\n")
    else:
        ap.print_help()


if __name__ == "__main__":
    main()
