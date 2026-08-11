# -*- coding: utf-8 -*-
"""
aplicar.py - pega o .json que o Ghost Map Editor gravou e escreve o .gbin novo.

Por que a escrita mora aqui e nao no C++: o round-trip load->save do
pet-source_tools ja foi provado semanticamente lossless nos 18 holes do Spring
Wind (so zera lixo de memoria que a Ntreev deixou depois do \\0 nos campos de
string fixa), e a formula das AABB foi validada em 3.269 elementos. Ter um
segundo escritor em C++ so criaria dois caminhos pra dar manutencao.

NAO ENCOSTA em: lights (tee/pin), MapCheck, nodes, cameras, base_element.
Isso e proposital -- e o que garante que a jogabilidade do hole continua a
mesma. O editor tambem nao deixa selecionar essas coisas.

Uso:
    python aplicar.py projeto.json [--saida PASTA] [--verboso]
"""
from __future__ import print_function

import argparse
import copy
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import petpkg

gbin = petpkg.gbin
pet = petpkg.pet


# ===========================================================================
#  INDICE DE ARQUIVOS
# ===========================================================================
class Indice:
    """Acha um .pet pelo nome do arquivo, varrendo as pastas que o editor usou.

    No empate fica com o caminho MAIS CURTO -- e o mesmo criterio do AssetDB
    do C++, entao o Python resolve pro mesmo arquivo que o editor mostrou na
    tela. Se divergisse, a AABB calculada aqui nao bateria com o que o usuario
    viu.
    """

    def __init__(self, raizes):
        self.mapa = {}
        for r in raizes:
            if not r or not os.path.isdir(r):
                continue
            for dirpath, _dirnames, arquivos in os.walk(r):
                for a in arquivos:
                    k = a.lower()
                    p = os.path.join(dirpath, a)
                    ant = self.mapa.get(k)
                    if ant is None or len(p) < len(ant):
                        self.mapa[k] = p

    def acha(self, nome):
        return self.mapa.get(os.path.basename(nome).lower())


_cache_pet = {}


def carrega_pet(indice, nome):
    """(vertices Nx3, n_poligonos, [nomes de textura]) do modelo, ou None."""
    k = nome.lower()
    if k in _cache_pet:
        return _cache_pet[k]
    caminho = indice.acha(nome)
    if not caminho or not os.path.exists(caminho):
        _cache_pet[k] = None
        return None
    pet.setFileType(caminho)
    P = pet.Puppet()
    with open(caminho, 'rb') as f:
        P.load(f)
    if P.mesh is None or not P.mesh.vertices:
        _cache_pet[k] = None
        return None
    V = np.array([[v.x, v.y, v.z] for v in P.mesh.vertices], dtype=np.float64)
    texturas = [t.fn for t in (P.textures or []) if getattr(t, 'fn', None)]
    r = (V, len(P.mesh.polygons), texturas)
    _cache_pet[k] = r
    return r


# ===========================================================================
#  AABB
# ===========================================================================
def calcula_aabb(V, matrix_world):
    """min_max e fit_base_model em MUNDO, na convencao vetor-LINHA (V @ M + T).

    Formula validada contra 3.269 elementos originais dos 18 holes (erro 0.000):
      * min_max        = AABB exata dos vertices transformados
      * fit_base_model = XZ dos vertices com y <= miny+1.0, e Y indo de miny
                         ate miny+10 (o 10 e literal, nao escala com o modelo)
    """
    M = np.array(matrix_world[:9], dtype=np.float64).reshape(3, 3)
    T = np.array(matrix_world[9:12], dtype=np.float64)
    W = V @ M + T
    mn = W.min(0)
    mx = W.max(0)

    k = W[:, 1] <= mn[1] + 1.0
    if not k.any():
        k = W[:, 1] <= mn[1] + 1e-3
    fmn = W[k].min(0)
    fmx = W[k].max(0)

    return (
        (float(mn[0]), float(mn[1]), float(mn[2]),
         float(mx[0]), float(mx[1]), float(mx[2])),
        (float(fmn[0]), float(mn[1]), float(fmn[2]),
         float(fmx[0]), float(mn[1] + 10.0), float(fmx[2])),
    )


def poe_aabb(elem, mm, fb):
    elem.min_max.minx, elem.min_max.miny, elem.min_max.minz = mm[0], mm[1], mm[2]
    elem.min_max.maxx, elem.min_max.maxy, elem.min_max.maxz = mm[3], mm[4], mm[5]
    elem.fit_base_model.minx, elem.fit_base_model.miny, elem.fit_base_model.minz = fb[0], fb[1], fb[2]
    elem.fit_base_model.maxx, elem.fit_base_model.maxy, elem.fit_base_model.maxz = fb[3], fb[4], fb[5]


# ===========================================================================
#  APLICACAO
# ===========================================================================
def aplica(projeto, dir_saida, verboso=False):
    caminho_gbin = projeto['gbin']
    if not os.path.exists(caminho_gbin):
        raise SystemExit("nao achei o .gbin de origem:\n  %s" % caminho_gbin)

    raizes = list(projeto.get('raizes') or [])
    raizes.append(os.path.dirname(caminho_gbin))
    indice = Indice(raizes)
    if verboso:
        print("indice: %d arquivos em %d raizes" % (len(indice.mapa), len(raizes)))

    gbin.setFileType(caminho_gbin)
    m = gbin.GBin()
    with open(caminho_gbin, 'rb') as f:
        m.load(f)

    n_orig = len(m.elements)
    if verboso:
        print("origem: %s  (%d elementos, versao 0x%X)"
              % (os.path.basename(caminho_gbin), n_orig, gbin.gVersion))

    if not m.elements:
        raise SystemExit("este .gbin nao tem nenhum elemento pra servir de molde")
    molde = m.elements[0]

    apagar = set()
    novos = []
    caixas = []
    faltando = []
    contas = {'add': 0, 'mover': 0, 'apagar': 0, 'soundbox': 0}

    def monta(elem, op, nome_modelo):
        """preenche matriz, flags, script e AABB de um elemento."""
        mw = [float(v) for v in op['matrix_world']]
        elem.name = nome_modelo
        elem.matrix_world = tuple(mw)
        elem.matrix_local = tuple(mw)
        elem.course_type = int(op.get('course_type', 0))
        elem.script = op.get('script', '') or ''
        elem.option = gbin.ElementOptionFlag(
            gbin.ElementOptionAnimateFlag.from_value(int(op.get('anim', 0))),
            gbin.ElementOptionCollisionFlag.from_value(int(op.get('coll', 0))))
        elem.extra_value = gbin.ExtraValueV72(-1, -1)

        dados = carrega_pet(indice, nome_modelo)
        if dados is None:
            faltando.append(nome_modelo)
            return False
        V, npoly, texturas = dados
        mm, fb = calcula_aabb(V, mw)
        poe_aabb(elem, mm, fb)
        elem.face_num = npoly

        # o cliente precisa das texturas do modelo declaradas no gbin -- e o
        # que o wizcity_02 (que tem portal) faz e o wizcity_01 (que nao tem)
        # nao faz. Vale pra qualquer modelo novo, nao so pro portal.
        ja = set(t.name for t in m.textures)
        for tn in texturas:
            if tn and tn not in ja:
                m.textures.append(gbin.Texture(tn))
                ja.add(tn)
        return True

    for op in projeto.get('operacoes', []):
        tipo = op.get('op')

        if tipo == 'apagar':
            i = int(op['indice'])
            if 0 <= i < n_orig:
                apagar.add(i)
                contas['apagar'] += 1
            continue

        if tipo == 'mover':
            i = int(op['indice'])
            if not (0 <= i < n_orig):
                continue
            e = m.elements[i]
            if monta(e, op, op.get('modelo', e.name)):
                contas['mover'] += 1
            continue

        if tipo == 'add':
            e = copy.deepcopy(molde)
            if monta(e, op, op['modelo']):
                novos.append(e)
                contas['add'] += 1
            continue

        if tipo == 'soundbox':
            mn = op['min']
            mx = op['max']
            caixa = gbin.AABB()
            caixa.minx, caixa.miny, caixa.minz = float(mn[0]), float(mn[1]), float(mn[2])
            caixa.maxx, caixa.maxy, caixa.maxz = float(mx[0]), float(mx[1]), float(mx[2])
            sb = gbin.SoundBox(type=int(op.get('tipo', 1)), name=op.get('nome', ''),
                               min_max_world=caixa)
            sb.extra_value = gbin.ExtraValueV72(-1, -1)
            caixas.append(sb)
            contas['soundbox'] += 1
            continue

        print("  [aviso] operacao desconhecida, ignorada: %r" % tipo)

    if faltando:
        print("  [aviso] %d modelo(s) nao encontrados nas pastas indexadas:"
              % len(set(faltando)))
        for n in sorted(set(faltando)):
            print("          %s" % n)
        print("          -> essas operacoes foram PULADAS. Aponte a pasta certa")
        print("             no editor (arraste a pasta pra janela) e gere de novo.")

    if apagar:
        m.elements = [e for i, e in enumerate(m.elements) if i not in apagar]
    m.elements.extend(novos)
    if caixas:
        m.sound_boxs.extend(caixas)

    os.makedirs(dir_saida, exist_ok=True)
    saida = os.path.join(dir_saida, os.path.basename(caminho_gbin))
    gbin.setFileType(saida)
    with open(saida, 'wb') as f:
        m.save(f)

    print("+%d novos  ~%d movidos  -%d apagados  +%d soundbox"
          % (contas['add'], contas['mover'], contas['apagar'], contas['soundbox']))
    print("elementos: %d -> %d" % (n_orig, len(m.elements)))
    print("gravado: %s" % saida)
    return saida


def main():
    ap = argparse.ArgumentParser(description="aplica um projeto do Ghost Map Editor num .gbin")
    ap.add_argument('projeto', help="o .json gravado pelo editor")
    ap.add_argument('--saida', default=None, help="pasta de saida (padrao: ./saida)")
    ap.add_argument('--verboso', action='store_true')
    a = ap.parse_args()

    with open(a.projeto, 'r', encoding='utf-8') as f:
        projeto = json.load(f)

    dir_saida = a.saida or os.path.join(os.path.dirname(os.path.abspath(a.projeto)), 'saida')
    aplica(projeto, dir_saida, a.verboso)


if __name__ == '__main__':
    main()
