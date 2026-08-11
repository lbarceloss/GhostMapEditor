# -*- coding: utf-8 -*-
"""Carrega gbin.py / pet.py do addon pet-source_tools SEM Blender.

O addon so precisa do `bpy` em util.py, e o gbin usa dali uma unica funcao
(getNewGUID) -- entao da pra falsificar o modulo e usar o resto headless.

O addon NAO vem junto neste repositorio: e um projeto de terceiro (Acrisio
Filho -- https://github.com/Acrisio-Filho/pet-source_tools). Baixe uma copia e
aponte pra ela de uma destas formas, nesta ordem de prioridade:

  1. variavel de ambiente  PET_SOURCE_TOOLS=C:\\caminho\\pet-source_tools
  2. arquivo de texto      pet_source_tools.txt  (ao lado do exe ou desta pasta),
                           contendo so o caminho da pasta
  3. uma das pastas vizinhas procuradas automaticamente (veja _CANDIDATOS)
"""
import os
import sys
import types
import importlib.util

_AQUI = os.path.dirname(os.path.abspath(__file__))   # ...\bin\tools
_BIN = os.path.dirname(_AQUI)                        # ...\bin
_RAIZ = os.path.dirname(_BIN)                        # raiz do repositorio

# pastas onde procurar o addon, em ordem. A primeira que tiver gbin.py ganha.
_CANDIDATOS = [
    os.path.join(_RAIZ, "pet-source_tools"),
    os.path.join(_BIN, "pet-source_tools"),
    os.path.join(_AQUI, "pet-source_tools"),
    os.path.join(os.path.dirname(_RAIZ), "pet-source_tools"),
    os.path.expanduser(r"~\Desktop\pet-source_tools"),
    os.path.expanduser(r"~\Desktop\Abrir Map Completo\pet-source_tools"),
]


def _valida(p):
    return bool(p) and os.path.isfile(os.path.join(p, "gbin.py"))


def acha_addon():
    """Devolve o caminho da pasta do pet-source_tools, ou None."""
    p = os.environ.get("PET_SOURCE_TOOLS")
    if _valida(p):
        return p

    for base in (_BIN, _AQUI, _RAIZ):
        txt = os.path.join(base, "pet_source_tools.txt")
        if os.path.isfile(txt):
            with open(txt, "r", encoding="utf-8-sig") as f:
                for linha in f:
                    linha = linha.strip().strip('"')
                    if linha and not linha.startswith("#") and _valida(linha):
                        return linha

    for c in _CANDIDATOS:
        if _valida(c):
            return c
    return None


def _boot():
    if "_petpkg" in sys.modules:
        return sys.modules["_petpkg.gbin"], sys.modules["_petpkg.pet"]

    addon = acha_addon()
    if addon is None:
        raise SystemExit(
            "nao achei o addon pet-source_tools (e ele que escreve o .gbin).\n"
            "  Baixe em: https://github.com/Acrisio-Filho/pet-source_tools\n"
            "  e aponte pra ele de uma destas formas:\n"
            "    - ponha a pasta 'pet-source_tools' na raiz deste projeto\n"
            "    - ou crie um 'pet_source_tools.txt' ao lado do exe com o caminho\n"
            "    - ou defina a variavel de ambiente PET_SOURCE_TOOLS"
        )

    pkg = types.ModuleType("_petpkg")
    pkg.__path__ = [addon]
    sys.modules["_petpkg"] = pkg

    def carrega(nome):
        caminho = os.path.join(addon, nome + ".py")
        spec = importlib.util.spec_from_file_location("_petpkg." + nome, caminho)
        mod = importlib.util.module_from_spec(spec)
        sys.modules["_petpkg." + nome] = mod
        spec.loader.exec_module(mod)
        return mod

    carrega("ioutil")

    # util.py precisa de bpy; o gbin so usa getNewGUID
    falso = types.ModuleType("_petpkg.util")
    _c = [10000]

    def getNewGUID():
        _c[0] += 1
        return _c[0]

    falso.getNewGUID = getNewGUID
    sys.modules["_petpkg.util"] = falso

    return carrega("gbin"), carrega("pet")


gbin, pet = _boot()
