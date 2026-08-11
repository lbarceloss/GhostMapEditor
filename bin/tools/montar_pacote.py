# -*- coding: utf-8 -*-
"""montar_pacote.py - junta os mapas e as texturas da SUA instalacao do PangYa
numa pasta so, do jeito que o Ghost Map Editor gosta de achar.

    mapas/
        round01_wind/...
        round10_spring wind/...
        texture_dds/...

Ponha essa pasta `mapas` AO LADO do GhostMapEditor.exe e pronto: o editor a
indexa sozinho e a lista de MAPA -> HOLE (tecla TAB) ja vem cheia, sem editar
o editor.cfg.

⚠️  O pacote e feito a partir dos arquivos que VOCE extraiu do seu cliente do
    PangYa. Ele e conteudo da Ntreev Soft / NCSOFT: use na sua maquina, nao
    republique.

Uso:
    python montar_pacote.py --mapas "C:\\...\\data" --texturas "C:\\...\\texture_dds"
    python montar_pacote.py --mapas "C:\\...\\data" --saida "C:\\...\\bin\\mapas" --modo link
"""
from __future__ import print_function

import argparse
import os
import shutil
import sys

# So o que o editor abre. O resto do cliente (som, ui, video) nao serve de nada
# aqui e so faria o pacote engordar. Com --tudo, nada e filtrado: vem tambem o
# .sbin (terreno/colisao, o grosso do peso), o _property.xml (tee, pino e as
# classes de booster) e o .wep (o projeto do editor da Ntreev).
EXTENSOES = {".gbin", ".pet", ".dds", ".jpg", ".jpeg", ".png", ".tga", ".bmp"}

# Pastas que nao interessam pro editor.
IGNORAR = {"sound", "music", "movie", "video", "ui", "font", "shader", "__pycache__"}


def humano(n):
    for u in ("B", "KB", "MB", "GB"):
        if n < 1024.0:
            return "%.1f %s" % (n, u)
        n /= 1024.0
    return "%.1f TB" % n


def varre(raiz, tudo=False):
    """(caminho absoluto, caminho relativo) de tudo que interessa em `raiz`."""
    for dirpath, dirnames, arquivos in os.walk(raiz):
        dirnames[:] = [d for d in dirnames if d.lower() not in IGNORAR]
        for a in arquivos:
            if not tudo and os.path.splitext(a)[1].lower() not in EXTENSOES:
                continue
            cheio = os.path.join(dirpath, a)
            yield cheio, os.path.relpath(cheio, raiz)


def poe(origem, destino, modo, contas):
    """copia (ou linka) um arquivo, pulando o que ja esta igual."""
    if os.path.exists(destino):
        try:
            if os.path.getsize(destino) == os.path.getsize(origem):
                contas["pulados"] += 1
                return 0
        except OSError:
            pass
        try:
            os.remove(destino)
        except OSError:
            pass

    pasta = os.path.dirname(destino)
    if pasta and not os.path.isdir(pasta):
        os.makedirs(pasta, exist_ok=True)

    tam = os.path.getsize(origem)
    if modo == "link":
        try:
            os.link(origem, destino)      # hardlink: instantaneo e nao ocupa disco
            contas["linkados"] += 1
            return tam
        except OSError:
            pass                          # volume diferente: cai pra copia
    shutil.copy2(origem, destino)
    contas["copiados"] += 1
    return tam


def main():
    ap = argparse.ArgumentParser(
        description="monta a pasta unica de mapas do Ghost Map Editor")
    ap.add_argument("--mapas", action="append", default=[], metavar="PASTA",
                    help="pasta 'data' dos mapas extraidos (pode repetir)")
    ap.add_argument("--texturas", action="append", default=[], metavar="PASTA",
                    help="pasta do pool de texturas, ex: texture_dds (pode repetir)")
    ap.add_argument("--saida", default=None,
                    help="pasta do pacote (padrao: 'mapas' ao lado do exe)")
    ap.add_argument("--modo", choices=("copiar", "link"), default="copiar",
                    help="'link' faz hardlink em vez de copiar: instantaneo e nao "
                         "ocupa disco, mas so funciona no mesmo volume e NAO serve "
                         "pra zipar e levar pra outra maquina")
    ap.add_argument("--tudo", action="store_true",
                    help="nao filtra por extensao: leva tambem .sbin (terreno), "
                         "_property.xml, .wep e o resto. Pra uso local, quando "
                         "voce quer a pasta do mapa inteira num lugar so")
    ap.add_argument("--simular", action="store_true",
                    help="so mostra o que faria")
    a = ap.parse_args()

    if not a.mapas and not a.texturas:
        ap.error("diga pelo menos --mapas ou --texturas")

    # padrao: <pasta do bin>\mapas  (este script mora em bin\tools\)
    saida = a.saida or os.path.join(
        os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "mapas")
    saida = os.path.abspath(saida)

    tarefas = []                       # (origem, destino)
    for raiz in a.mapas:
        raiz = os.path.abspath(raiz)
        if not os.path.isdir(raiz):
            print("[aviso] nao achei: %s" % raiz)
            continue
        for cheio, rel in varre(raiz, a.tudo):
            tarefas.append((cheio, os.path.join(saida, rel)))

    for raiz in a.texturas:
        raiz = os.path.abspath(raiz)
        if not os.path.isdir(raiz):
            print("[aviso] nao achei: %s" % raiz)
            continue
        # o pool de texturas vira sempre 'texture_dds' dentro do pacote
        base = os.path.join(saida, "texture_dds")
        for cheio, rel in varre(raiz, a.tudo):
            tarefas.append((cheio, os.path.join(base, rel)))

    if not tarefas:
        print("nada pra fazer: nenhuma pasta tinha arquivo que o editor use")
        return 1

    total = sum(os.path.getsize(o) for o, _ in tarefas)
    print("pacote:  %s" % saida)
    print("modo:    %s%s" % (a.modo, "   (tudo)" if a.tudo else "   (so o que o editor abre)"))
    print("%d arquivos, %s" % (len(tarefas), humano(total)))
    gbins = sum(1 for o, _ in tarefas if o.lower().endswith(".gbin"))
    print("%d .gbin (holes)" % gbins)

    if a.simular:
        print("\n(--simular: nada foi escrito)")
        return 0

    contas = {"copiados": 0, "linkados": 0, "pulados": 0}
    feito = 0
    for i, (origem, destino) in enumerate(tarefas):
        feito += poe(origem, destino, a.modo, contas)
        if (i % 500) == 0 or i == len(tarefas) - 1:
            sys.stdout.write("\r  %d/%d  (%s)   " % (i + 1, len(tarefas), humano(feito)))
            sys.stdout.flush()
    print()

    leiame = os.path.join(saida, "LEIA-ME.txt")
    with open(leiame, "w", encoding="utf-8") as f:
        f.write(
            "Pacote de mapas do Ghost Map Editor\n"
            "===================================\n\n"
            "Deixe esta pasta com o nome 'mapas', ao lado do GhostMapEditor.exe.\n"
            "O editor indexa ela sozinho: abra o programa e aperte TAB pra\n"
            "escolher o mapa e o hole.\n\n"
            "Estes arquivos sao do cliente do PangYa (Ntreev Soft / NCSOFT).\n"
            "Foram montados a partir da sua propria instalacao -- nao republique.\n")

    print("copiados %d   linkados %d   ja estavam la %d"
          % (contas["copiados"], contas["linkados"], contas["pulados"]))
    print("\n[OK] agora deixe essa pasta como 'mapas' ao lado do GhostMapEditor.exe")
    print("     abra o editor e aperte TAB.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
