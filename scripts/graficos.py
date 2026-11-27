#!/usr/bin/env python3
"""Gera tabelas-resumo e graficos a partir dos CSVs em results/.

Uso: python3 scripts/graficos.py
Saidas: results/resumo_*.csv e results/figuras/*.png
"""
import os
import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

RAIZ = os.path.join(os.path.dirname(__file__), "..")
RES = os.path.join(RAIZ, "results")
FIG = os.path.join(RES, "figuras")
os.makedirs(FIG, exist_ok=True)

TINTA, PAPEL, LARANJA, AZUL, CINZA = "#1B2430", "#F5F3EE", "#E36414", "#2A6F97", "#8A8F98"
plt.rcParams.update({
    "figure.facecolor": PAPEL, "axes.facecolor": PAPEL, "savefig.facecolor": PAPEL,
    "axes.edgecolor": CINZA, "axes.labelcolor": TINTA, "xtick.color": TINTA, "ytick.color": TINTA,
    "text.color": TINTA, "axes.spines.top": False, "axes.spines.right": False,
    "axes.grid": True, "grid.color": "#DDD8CE", "grid.linewidth": 0.8,
    "font.size": 13, "axes.titlesize": 15, "axes.titleweight": "bold", "legend.frameon": False,
})


def salvar(fig, nome):
    fig.tight_layout()
    fig.savefig(os.path.join(FIG, nome), dpi=160)
    plt.close(fig)


# ------------------------------------------------------------------ montanha
def montanha():
    df = pd.read_csv(os.path.join(RES, "montanha.csv"))
    g = df.groupby(["cenario", "C", "m"])
    res = g.agg(
        reps=("T_s", "size"),
        T_media=("T_s", "mean"), T_dp=("T_s", "std"),
        vazao_media=("vazao", "mean"), vazao_dp=("vazao", "std"),
        lat_media_ms=("lat_media_ms", "mean"), lat_p95_ms=("lat_p95_ms", "mean"),
        ms_por_volta=("ms_por_volta", "mean"), ms_por_volta_dp=("ms_por_volta", "std"),
        cpu_pct=("cpu_pct", "mean"), trocas_vol=("trocas_vol", "mean"),
        trocas_invol=("trocas_invol", "mean"), max_na_pista=("max_na_pista", "mean"),
        violacoes=("violacoes", "sum"),
    ).reset_index()
    # speedup e eficiencia (cenario 1), referencia T(1) do mesmo C.
    # Como cada carro faz R voltas, o trabalho total (m*R voltas) cresce com m.
    # S_literal = T(1)/T(m) compara execucoes de tamanhos diferentes; por isso
    # tambem calculamos o speedup a trabalho igual: um carro levaria m*T(1)
    # para fazer as mesmas m*R voltas, logo S(m) = m*T(1)/T(m) = vazao(m)/vazao(1).
    res["speedup_literal"] = float("nan")
    res["speedup"] = float("nan")
    res["eficiencia"] = float("nan")
    for C in res.C.unique():
        sel = (res.cenario == "c1_fixo") & (res.C == C)
        if sel.any():
            t1 = res.loc[sel & (res.m == 1), "T_media"].iloc[0]
            res.loc[sel, "speedup_literal"] = t1 / res.loc[sel, "T_media"]
            res.loc[sel, "speedup"] = res.loc[sel, "m"] * t1 / res.loc[sel, "T_media"]
            res.loc[sel, "eficiencia"] = res.loc[sel, "speedup"] / res.loc[sel, "m"]
    res.to_csv(os.path.join(RES, "resumo_montanha.csv"), index=False, float_format="%.5g")

    c = lambda cen, C=4: res[(res.cenario == cen) & (res.C == C)]

    fig, ax = plt.subplots(figsize=(8, 4.8))
    for cen, cor, rot in [("c1_fixo", AZUL, "Cenário 1: n = 200"), ("c2_proporcional", LARANJA, "Cenário 2: n = 2mC")]:
        d = c(cen)
        ax.errorbar(d.m, d.T_media, yerr=d.T_dp, marker="o", color=cor, label=rot, capsize=3)
    ax.set(xlabel="carros (m)", ylabel="tempo total T(m) (s)", title="Tempo total para m·R voltas (C = 4, R = 20)")
    ax.legend(); salvar(fig, "montanha_tempo.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    for C, cor in [(2, CINZA), (4, AZUL), (8, LARANJA)]:
        d = c("c2_proporcional", C)
        ax.errorbar(d.m, d.vazao_media, yerr=d.vazao_dp, marker="o", color=cor, label=f"C = {C}", capsize=3)
    ax.set(xlabel="carros (m)", ylabel="vazão λ(m) (passageiros/s)", title="Vazão satura perto de m ≈ 12 (cenário 2)")
    ax.legend(); salvar(fig, "montanha_vazao.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    d = c("c1_fixo")
    ax.plot(d.m, d.m, ls="--", color=CINZA, label="ideal (S = m)")
    ax.plot(d.m, d.speedup, marker="o", color=AZUL, label="speedup S(m) = m·T(1)/T(m)")
    ax.axhline(12.56 / 1.18, color=AZUL, ls=":", lw=1)
    ax.set(xlabel="carros (m)", ylabel="speedup (trabalho igual)", title="Speedup satura em ≈ 10,6 (cenário 1, C = 4)")
    ax.set_ylim(0, 16)
    ax2 = ax.twinx(); ax2.grid(False)
    ax2.plot(d.m, d.eficiencia * 100, marker="s", color=LARANJA, label="eficiência E(m)")
    ax2.set_ylabel("eficiência (%)", color=LARANJA); ax2.set_ylim(0, 110)
    ax2.spines["right"].set_visible(True)
    h1, l1 = ax.get_legend_handles_labels(); h2, l2 = ax2.get_legend_handles_labels()
    ax.legend(h1 + h2, l1 + l2, loc="center right"); salvar(fig, "montanha_speedup.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    for cen, cor, rot in [("c1_fixo", AZUL, "Cenário 1: n = 200"), ("c2_proporcional", LARANJA, "Cenário 2: n = 2mC")]:
        d = c(cen)
        ax.plot(d.m, d.lat_media_ms, marker="o", color=cor, label=rot + " (média)")
        ax.plot(d.m, d.lat_p95_ms, ls=":", color=cor, label=rot + " (p95)")
    ax.set(xlabel="carros (m)", ylabel="espera para embarcar (ms)", title="Latência de espera (board)")
    ax.legend(fontsize=11); salvar(fig, "montanha_latencia.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    for C, cor in [(2, CINZA), (4, AZUL), (8, LARANJA)]:
        d = c("c3_sem_trabalho", C)
        ax.errorbar(d.m, d.ms_por_volta * 1000, yerr=d.ms_por_volta_dp * 1000, marker="o", color=cor, label=f"C = {C}", capsize=3)
    ax.set(xlabel="carros (m)", ylabel="tempo por volta (µs)", title="Sobrecarga pura dos semáforos (cenário 3)")
    ax.legend(); salvar(fig, "montanha_sobrecarga.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    d = c("c2_proporcional")
    ax.plot(d.m, d.trocas_vol, marker="o", color=AZUL, label="voluntárias")
    ax.plot(d.m, d.trocas_invol, marker="s", color=LARANJA, label="involuntárias")
    ax.set(xlabel="carros (m)", ylabel="trocas de contexto por execução", title="Trocas de contexto (cenário 2, C = 4)")
    ax2 = ax.twinx(); ax2.grid(False); ax2.spines["right"].set_visible(True)
    ax2.plot(d.m, d.cpu_pct, ls="--", color=CINZA, label="uso de CPU")
    ax2.set_ylabel("uso de CPU (%)")
    h1, l1 = ax.get_legend_handles_labels(); h2, l2 = ax2.get_legend_handles_labels()
    ax.legend(h1 + h2, l1 + l2, loc="upper left"); salvar(fig, "montanha_cpu.png")
    return res


# ------------------------------------------------------------------ babuinos
def babuinos():
    df = pd.read_csv(os.path.join(RES, "babuinos.csv"), na_values=["NA"])
    ok = df[df.cenario != "E_watchdog"]
    res = ok.groupby(["cenario", "solucao", "N", "p0", "chegada_ms"]).agg(
        reps=("T_s", "size"), T_media=("T_s", "mean"), vazao=("vazao", "mean"),
        espera_media_ms=("espera_media_ms", "mean"), espera_p95_ms=("espera_p95_ms", "mean"),
        espera_max_ms=("espera_max_ms", "max"), espera_max_d0_ms=("espera_max_d0_ms", "max"),
        espera_max_d1_ms=("espera_max_d1_ms", "max"), max_total=("max_total", "max"),
        violacoes=("violacoes", "sum"), trocas_sentido=("trocas_sentido", "mean"),
        deadlocks=("deadlock", "sum"), cpu_pct=("cpu_pct", "mean"),
    ).reset_index()
    res.to_csv(os.path.join(RES, "resumo_babuinos.csv"), index=False, float_format="%.5g")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    for sol, cor in [("justa", AZUL), ("ingenua", LARANJA)]:
        d = res[(res.cenario == "A_taxa") & (res.solucao == sol)].copy()
        d["taxa"] = 1000.0 / d.chegada_ms
        d = d.sort_values("taxa")
        ax.plot(d.taxa, d.espera_media_ms, marker="o", color=cor, label=f"{sol} (média)")
        ax.plot(d.taxa, d.espera_max_ms, ls=":", marker=".", color=cor, label=f"{sol} (máximo)")
    ax.set(xlabel="taxa de chegada (babuínos/s)", ylabel="espera (ms)", title="Espera cresce com a carga (N = 100, 50/50)")
    ax.legend(fontsize=11); salvar(fig, "babuinos_carga.png")

    fig, ax = plt.subplots(figsize=(8, 4.8))
    d = res[res.cenario == "C_fluxo"].sort_values(["chegada_ms", "solucao"])
    rot = [f"{s}\n{int(a)} ms" for s, a in zip(d.solucao, d.chegada_ms)]
    cores = [AZUL if s == "justa" else LARANJA for s in d.solucao]
    ax.bar(rot, d.espera_max_d1_ms, color=cores)
    for i, v in enumerate(d.espera_max_d1_ms):
        ax.text(i, v + 60, f"{v:.0f}", ha="center")
    ax.set(ylabel="espera máxima do sentido minoritário (ms)", title="Sem catraca, a minoria espera o fluxo inteiro")
    salvar(fig, "babuinos_inanicao.png")
    return res


# ------------------------------------------------------------------ h2o
def h2o():
    df = pd.read_csv(os.path.join(RES, "h2o.csv"), na_values=["NA"])
    res = df.groupby(["versao", "moleculas"]).agg(
        reps=("rep", "size"),
        validas=("resultado", lambda s: (s == "valida").sum()),
        invalidas=("resultado", lambda s: (s == "INVALIDA").sum()),
        travou=("resultado", lambda s: (s == "TRAVOU").sum()),
        T_media=("T_s", "mean"), moleculas_por_s=("moleculas_por_s", "mean"),
    ).reset_index()
    res.to_csv(os.path.join(RES, "resumo_h2o.csv"), index=False, float_format="%.5g")
    return res


if __name__ == "__main__":
    for nome, f in [("montanha", montanha), ("babuinos", babuinos), ("h2o", h2o)]:
        if os.path.exists(os.path.join(RES, nome + ".csv")):
            print(f().to_string(max_rows=200))
    print("figuras em", FIG)
