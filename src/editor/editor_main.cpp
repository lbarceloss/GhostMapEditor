// ============================================================================
//  Ghost Map Editor - editor de posicoes de objetos do .gbin do PangYa
//
//  NAO cria mapa do zero: carrega um hole que existe, deixa voce navegar em
//  primeira pessoa, escolher um .pet do catalogo, plantar, mover, girar,
//  escalar e apagar -- e grava a edicao num .json que o tools/aplicar.py
//  transforma num .gbin novo.
//
//  Por que o .gbin nao e escrito aqui em C++: o round-trip em Python
//  (pet-source_tools/gbin.py) ja foi provado semanticamente lossless nos 18
//  holes do Spring Wind, e a formula das AABB (min_max / fit_base_model) foi
//  validada em 3.269 elementos. Reimplementar isso em C++ so criaria um
//  segundo caminho de escrita pra dar manutencao.
//
//  Render, leitura de .gbin/.pet/.dds e raycast no terreno vem do
//  viewer_core, compartilhado com o Ghost Pangya SIM.
//
//  desenvolvido por Ghost - www.hkfirewall.com
// ============================================================================
#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

#include "viewer_core.h"
#include "pangya_gbin.h"
#include "pangya_pet.h"
#include "asset_db.h"
#include "platform_win.h"
#include "ground.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>

static const char* kTitulo = "Ghost Map Editor   |   by Ghost   |   www.hkfirewall.com";
static const char* kCredito = "Ghost Map Editor   -   desenvolvido por Ghost   -   www.hkfirewall.com";
static const char* kUrl = "https://www.hkfirewall.com";
static const char* kVersao = "1.0";

// ===========================================================================
//  DOCUMENTO DE EDICAO
// ===========================================================================

// Um elemento do mapa, do ponto de vista do editor.
//
// A matriz 3x3 ORIGINAL do elemento e preservada em `baseM` e nunca e
// decomposta: os elementos da Ntreev podem ter inclinacao e escala nao
// uniforme, e decompor pra "rotacao em Y + escala" iria corromper isso.
// O que o editor aplica e um DELTA: M_final = baseM * (escala * Ry(giro)).
// Elemento novo nasce com baseM = identidade, entao pra ele a conta bate
// exatamente com a do plantar.py.
struct EdElem {
    int         orig = -1;              // indice no gbin original; -1 = novo
    std::string modelo;                 // nome do arquivo .pet
    float       baseM[9] = { 1,0,0, 0,1,0, 0,0,1 };
    Vector3     pos = { 0,0,0 };        // posicao em coordenadas PangYa
    float       giro = 0.0f;            // rotacao extra em Y (radianos)
    float       escala = 1.0f;          // escala extra
    bool        apagado = false;
    bool        tocado = false;         // false + orig>=0 => nao vira operacao
    bool        portal = false;         // booster (script + anim=4 + coll=0)
    std::string script;                 // os 2 floats do booster
    int         animFlag = 0, collFlag = 0;
    int         courseType = 0;

    PetModel*   modelo3d = nullptr;     // cache; dono e a Scene
    AABBf       aabbArquivo;            // min_max lido do gbin (fallback visual)
};

// Soundbox nova. Tipo 1 com script no nome faz spawn de NPC:
//   "*type 0 *pet NPC_SeaGull.pet *num 5"
struct EdCaixa {
    std::string nome = "*type 0 *pet NPC_SeaGull.pet *num 5";
    int         tipo = 1;
    Vector3     pos = { 0,0,0 };        // centro, coordenadas PangYa
    float       raio = 120.0f;          // meia-aresta da caixa
};

struct Doc {
    std::vector<EdElem>  elems;
    std::vector<EdCaixa> caixas;
};

// --------------------------------------------------------------- historico --
struct Historico {
    std::vector<Doc> pilha;
    int cursor = -1;
    static const int LIMITE = 80;

    void Reset(const Doc& d) {
        pilha.clear();
        pilha.push_back(d);
        cursor = 0;
    }
    // Chame DEPOIS de aplicar a mudanca no doc.
    void Marca(const Doc& d) {
        if (cursor >= 0 && cursor + 1 < (int)pilha.size())
            pilha.erase(pilha.begin() + cursor + 1, pilha.end());
        pilha.push_back(d);
        if ((int)pilha.size() > LIMITE) pilha.erase(pilha.begin());
        cursor = (int)pilha.size() - 1;
    }
    bool PodeDesfazer() const { return cursor > 0; }
    bool PodeRefazer() const { return cursor >= 0 && cursor + 1 < (int)pilha.size(); }
    const Doc& Desfaz() { if (PodeDesfazer()) cursor--; return pilha[cursor]; }
    const Doc& Refaz()  { if (PodeRefazer()) cursor++; return pilha[cursor]; }
};

// ===========================================================================
//  MATRIZES
// ===========================================================================

// M = baseM * (escala * Ry(giro)), convencao vetor-LINHA (v @ M + T),
// que e como o .gbin guarda. Saida = os 12 floats do matrix_world.
static void MatrizDe(const EdElem& e, float out[12]) {
    const float c = cosf(e.giro) * e.escala;
    const float s = sinf(e.giro) * e.escala;
    const float R[9] = {  c, 0.0f,   -s,
                       0.0f, e.escala, 0.0f,
                          s, 0.0f,    c };
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            float v = 0;
            for (int k = 0; k < 3; k++) v += e.baseM[i * 3 + k] * R[k * 3 + j];
            out[i * 3 + j] = v;
        }
    out[9] = e.pos.x; out[10] = e.pos.y; out[11] = e.pos.z;
}

static Matrix MatrizRender(const EdElem& e) {
    float m[12];
    MatrizDe(e, m);
    return MatrixFromPangya(m);
}

// AABB do elemento em coordenadas de MUNDO (as do render, com Z invertido).
static BoundingBox CaixaMundo(const EdElem& e) {
    const Matrix xf = MatrizRender(e);
    Vector3 mn, mx;
    if (e.modelo3d && e.modelo3d->ok) {
        mn = e.modelo3d->bbox.min;
        mx = e.modelo3d->bbox.max;
    } else {
        // sem modelo carregado: usa a caixa que o proprio gbin declara,
        // que ja esta em mundo -- entao devolve direto.
        Vector3 a = PangyaToWorld(e.aabbArquivo.minx, e.aabbArquivo.miny, e.aabbArquivo.minz);
        Vector3 b = PangyaToWorld(e.aabbArquivo.maxx, e.aabbArquivo.maxy, e.aabbArquivo.maxz);
        BoundingBox bb;
        bb.min = Vector3{ fminf(a.x,b.x), fminf(a.y,b.y), fminf(a.z,b.z) };
        bb.max = Vector3{ fmaxf(a.x,b.x), fmaxf(a.y,b.y), fmaxf(a.z,b.z) };
        if (bb.max.x - bb.min.x < 1.0f) {   // caixa degenerada: da um volume minimo
            Vector3 p = PangyaToWorld(e.pos.x, e.pos.y, e.pos.z);
            bb.min = Vector3{ p.x - 15, p.y, p.z - 15 };
            bb.max = Vector3{ p.x + 15, p.y + 40, p.z + 15 };
        }
        return bb;
    }
    // transforma os 8 cantos
    Vector3 lo = { 1e30f, 1e30f, 1e30f }, hi = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < 8; i++) {
        Vector3 c = { (i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z };
        c = Vector3Transform(c, xf);
        lo.x = fminf(lo.x, c.x); lo.y = fminf(lo.y, c.y); lo.z = fminf(lo.z, c.z);
        hi.x = fmaxf(hi.x, c.x); hi.y = fmaxf(hi.y, c.y); hi.z = fmaxf(hi.z, c.z);
    }
    BoundingBox bb; bb.min = lo; bb.max = hi;
    return bb;
}

// ===========================================================================
//  RAYCAST NO TERRENO
// ===========================================================================
// Marcha ao longo do raio procurando a troca de sinal entre a altura do raio e
// a do terreno, depois refina por bissecao. O passo cresce com a distancia
// porque perto da camera precisa de precisao e longe nao.
static bool RaioNoChao(const Scene& sc, Ray r, Vector3& hit) {
    if (sc.ground.Empty()) return false;

    float t = 2.0f;
    float dAnt = 0.0f;
    bool  temAnt = false;
    float tAnt = t;

    for (int i = 0; i < 4000 && t < 30000.0f; i++) {
        Vector3 p = Vector3Add(r.position, Vector3Scale(r.direction, t));
        float y;
        if (sc.ground.HeightAt(p.x, p.z, y)) {
            const float d = p.y - y;
            if (temAnt && ((dAnt > 0 && d <= 0) || (dAnt < 0 && d >= 0))) {
                float a = tAnt, b = t;
                for (int k = 0; k < 48; k++) {
                    const float m = (a + b) * 0.5f;
                    Vector3 q = Vector3Add(r.position, Vector3Scale(r.direction, m));
                    float qy;
                    if (!sc.ground.HeightAt(q.x, q.z, qy)) break;
                    if ((q.y - qy) * (dAnt > 0 ? 1.0f : -1.0f) > 0) a = m; else b = m;
                }
                const float m = (a + b) * 0.5f;
                hit = Vector3Add(r.position, Vector3Scale(r.direction, m));
                float hy;
                if (sc.ground.HeightAt(hit.x, hit.z, hy)) hit.y = hy;
                return true;
            }
            dAnt = d; temAnt = true;
        } else {
            temAnt = false;
        }
        tAnt = t;
        t += 6.0f + t * 0.02f;
    }
    return false;
}

// ===========================================================================
//  CATALOGO DE OBJETOS
// ===========================================================================
struct ItemCat {
    std::string caminho;
    std::string nome;
    Texture2D   thumb = { 0 };
    bool        pediuThumb = false;
    int         usos = 0;               // quantas vezes aparece neste hole
};

struct Catalogo {
    std::vector<ItemCat> itens;
    std::string busca;
    int  scroll = 0;
    bool soUsados = false;
    // Miniaturas NAO podem ser geradas durante o desenho da lista: o
    // BeginScissorMode ativo recorta tambem o framebuffer da miniatura e ela
    // sai vazia. Entao a lista so enfileira, e quem gera e o inicio do quadro
    // seguinte, antes do BeginDrawing (mesmo esquema do provador do SIM).
    std::vector<int> fila;

    void Monta(const AssetDB& db, const GBin& g) {
        itens.clear();
        std::unordered_map<std::string, int> conta;
        for (const auto& e : g.elements) conta[ToLower(e.name)]++;

        for (const std::string& p : db.FindAllBySuffix(".pet")) {
            ItemCat it;
            it.caminho = p;
            it.nome = PathFileName(p);
            auto f = conta.find(ToLower(it.nome));
            it.usos = (f == conta.end()) ? 0 : f->second;
            itens.push_back(it);
        }
        std::sort(itens.begin(), itens.end(), [](const ItemCat& a, const ItemCat& b) {
            if ((a.usos > 0) != (b.usos > 0)) return a.usos > 0;   // usados primeiro
            return ToLower(a.nome) < ToLower(b.nome);
        });
    }
    void Descarrega() {
        for (auto& i : itens) if (i.thumb.id) UnloadTexture(i.thumb);
        itens.clear();
        fila.clear();
    }
    // indices que passam no filtro atual
    std::vector<int> Filtrados() const {
        std::vector<int> r;
        const std::string b = ToLower(busca);
        for (int i = 0; i < (int)itens.size(); i++) {
            if (soUsados && itens[i].usos == 0) continue;
            if (!b.empty() && ToLower(itens[i].nome).find(b) == std::string::npos) continue;
            r.push_back(i);
        }
        return r;
    }
};

// ===========================================================================
//  JSON (escrita a mao; o formato e simples e nao vale uma dependencia)
// ===========================================================================
static std::string EscapaJson(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if      (c == '\\') o += "\\\\";
        else if (c == '"')  o += "\\\"";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20)  { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}

static bool SalvaJson(const std::string& caminho, const std::string& gbinPath,
                      const std::vector<std::string>& raizes, const Doc& doc) {
    FILE* f = fopen(caminho.c_str(), "w");
    if (!f) return false;

    fprintf(f, "{\n");
    fprintf(f, "  \"gerado_por\": \"Ghost Map Editor %s\",\n", kVersao);
    fprintf(f, "  \"gbin\": \"%s\",\n", EscapaJson(gbinPath).c_str());
    // as mesmas pastas que o editor indexou, pra o aplicar.py achar os .pet
    fprintf(f, "  \"raizes\": [");
    for (size_t i = 0; i < raizes.size(); i++)
        fprintf(f, "%s\"%s\"", i ? ", " : "", EscapaJson(raizes[i]).c_str());
    fprintf(f, "],\n");
    fprintf(f, "  \"operacoes\": [\n");

    bool primeiro = true;
    auto virgula = [&]() { if (!primeiro) fprintf(f, ",\n"); primeiro = false; };

    for (const auto& e : doc.elems) {
        if (e.orig >= 0 && e.apagado) {
            virgula();
            fprintf(f, "    {\"op\": \"apagar\", \"indice\": %d, \"modelo\": \"%s\"}",
                    e.orig, EscapaJson(e.modelo).c_str());
            continue;
        }
        if (e.apagado) continue;                    // novo que foi apagado: some
        if (e.orig >= 0 && !e.tocado) continue;     // original intacto: nao mexe

        float m[12];
        MatrizDe(e, m);
        virgula();
        if (e.orig >= 0) fprintf(f, "    {\"op\": \"mover\", \"indice\": %d", e.orig);
        else             fprintf(f, "    {\"op\": \"add\"");
        fprintf(f, ", \"modelo\": \"%s\"", EscapaJson(e.modelo).c_str());
        fprintf(f, ", \"matrix_world\": [");
        for (int i = 0; i < 12; i++) fprintf(f, "%s%.6f", i ? ", " : "", m[i]);
        fprintf(f, "]");
        fprintf(f, ", \"anim\": %d, \"coll\": %d, \"course_type\": %d",
                e.animFlag, e.collFlag, e.courseType);
        if (!e.script.empty())
            fprintf(f, ", \"script\": \"%s\"", EscapaJson(e.script).c_str());
        if (e.portal) fprintf(f, ", \"portal\": true");
        fprintf(f, "}");
    }

    for (const auto& c : doc.caixas) {
        virgula();
        fprintf(f, "    {\"op\": \"soundbox\", \"tipo\": %d, \"nome\": \"%s\"",
                c.tipo, EscapaJson(c.nome).c_str());
        fprintf(f, ", \"min\": [%.4f, %.4f, %.4f], \"max\": [%.4f, %.4f, %.4f]}",
                c.pos.x - c.raio, c.pos.y - c.raio, c.pos.z - c.raio,
                c.pos.x + c.raio, c.pos.y + c.raio, c.pos.z + c.raio);
    }

    fprintf(f, "\n  ]\n}\n");
    fclose(f);
    return true;
}

// ===========================================================================
//  UI - helpers
// ===========================================================================
static void Painel(Rectangle r, const char* titulo) {
    DrawRectangleRec(r, Color{ 18, 21, 28, 232 });
    DrawRectangleLinesEx(r, 1, Color{ 62, 74, 92, 255 });
    if (titulo) {
        DrawRectangle((int)r.x, (int)r.y, (int)r.width, 26, Color{ 30, 36, 48, 255 });
        DrawText(titulo, (int)r.x + 10, (int)r.y + 6, 15, Color{ 190, 210, 235, 255 });
    }
}

// Campo de texto imediato. Devolve true se o conteudo mudou.
static bool CampoTexto(Rectangle r, std::string& s, bool ativo, const char* dica) {
    DrawRectangleRec(r, ativo ? Color{ 30, 40, 56, 255 } : Color{ 26, 30, 40, 255 });
    DrawRectangleLinesEx(r, 1, ativo ? Color{ 120, 180, 255, 255 } : Color{ 66, 78, 96, 255 });

    bool mudou = false;
    if (ativo) {
        int c;
        while ((c = GetCharPressed()) > 0) {
            if (c >= 32 && c < 127) { s += (char)c; mudou = true; }
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
            if (!s.empty()) { s.pop_back(); mudou = true; }
        }
    }
    const char* txt = s.empty() ? dica : s.c_str();
    const Color col = s.empty() ? Color{ 110, 122, 140, 255 } : RAYWHITE;
    // mostra o final do texto se nao couber
    int fs = 14;
    std::string vis = txt;
    while (MeasureText(vis.c_str(), fs) > r.width - 14 && vis.size() > 1)
        vis.erase(0, 1);
    DrawText(vis.c_str(), (int)r.x + 7, (int)r.y + (int)(r.height - fs) / 2, fs, col);
    if (ativo && ((int)(GetTime() * 2) % 2) == 0) {
        int w = MeasureText(vis.c_str(), fs);
        DrawRectangle((int)r.x + 8 + w, (int)r.y + 5, 2, (int)r.height - 10, Color{ 150, 200, 255, 255 });
    }
    return mudou;
}

static void Linha(int x, int& y, const char* rot, const char* val, Color cv = RAYWHITE) {
    DrawText(rot, x, y, 14, Color{ 140, 156, 178, 255 });
    DrawText(val, x + 92, y, 14, cv);
    y += 19;
}

// ===========================================================================
//  MAIN
// ===========================================================================
enum class Modo { Navegar, Plantar, PorCaixa };

int main(int argc, char** argv) {
    Config cfg;
    LoadConfig("editor.cfg", cfg);
    if (cfg.extraRoots.empty()) LoadConfig("ball.cfg", cfg);   // reaproveita o do SIM

    // --shot renderiza uns quadros e sai. Serve pra conferir a tela sem
    // ficar clicando na mao (foi assim que o editor foi validado).
    std::string cmdGbin, shotOut, shotBusca, shotAuto;
    Vector3 shotCam = { 0,0,0 }, shotLook = { 0,0,0 };
    bool temShotCam = false, temShotLook = false;
    int  shotPlantar = -1;      // indice na lista filtrada do catalogo
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "--shot" && i + 1 < argc) shotOut = argv[++i];
        else if (a == "--cam"  && i + 1 < argc) temShotCam  = sscanf(argv[++i], "%f,%f,%f", &shotCam.x,  &shotCam.y,  &shotCam.z)  == 3;
        else if (a == "--look" && i + 1 < argc) temShotLook = sscanf(argv[++i], "%f,%f,%f", &shotLook.x, &shotLook.y, &shotLook.z) == 3;
        else if (a == "--plantar" && i + 1 < argc) shotPlantar = atoi(argv[++i]);
        else if (a == "--buscar" && i + 1 < argc)  shotBusca = argv[++i];
        // --auto: planta no cursor e grava o projeto. E o auto-teste do
        // caminho C++ -> .json -> aplicar.py -> .gbin, sem depender de clique.
        else if (a == "--auto" && i + 1 < argc)    shotAuto = argv[++i];
        else if (!a.empty() && a[0] != '-') cmdGbin = a;
    }
    const bool modoShot = !shotOut.empty();
    int quadrosShot = 0;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1500, 900, kTitulo);
    SetExitKey(KEY_NULL);
    SetTargetFPS(0);
    rlSetClipPlanes(1.0, 80000.0);

    ViewShader vs;
    vs.Load();
    Material mat = LoadMaterialDefault();
    mat.shader = vs.sh;
    RenderTexture2D fboThumb = LoadRenderTexture(96, 96);

    Scene scene;
    FreeCam cam;
    cam.speed = cfg.moveSpeed;
    cam.fov = cfg.fov;

    Doc doc;
    Historico hist;
    Catalogo cat;

    Modo modo = Modo::Navegar;
    int  sel = -1;              // indice em doc.elems
    int  selCaixa = -1;         // indice em doc.caixas
    int  plantando = -1;        // indice em cat.itens
    bool arrastando = false;    // movendo o selecionado com o mouse
    bool temGhost = false;
    Vector3 ghost = { 0,0,0 };
    float  novoGiro = 0.0f, novaEscala = 1.0f, alturaExtra = 0.0f;
    bool   comoPortal = false;

    bool mostraCatalogo = true;
    bool mostraAjuda = false;
    bool mostraMarcadores = true;
    bool mostraCaixas = false;
    bool mostraSky = true;
    bool fogOn = true;
    bool editandoTexto = false;     // trava as teclas de andar
    int  campoAtivo = 0;            // 0=nenhum 1=busca 2=script portal 3=script caixa
    std::string msg;
    float msgAte = 0.0f;
    std::string arquivoProjeto;
    bool gerarPendente = false;

    auto aviso = [&](const std::string& t, float seg = 4.0f) {
        msg = t;
        msgAte = (float)GetTime() + seg;
    };

    // ------------------------------------------------------------ carregar --
    auto modeloDe = [&](const std::string& nome) -> PetModel* {
        const std::string key = ToLower(nome);
        auto it = scene.models.find(key);
        if (it != scene.models.end()) return it->second;
        const std::string p = scene.db.Find(nome);
        if (p.empty()) { scene.models[key] = nullptr; return nullptr; }
        Pet pet;
        if (!LoadPet(p.c_str(), pet)) { scene.models[key] = nullptr; return nullptr; }
        PetModel* pm = BuildModel(pet, scene.db, scene.tex, nullptr);
        scene.models[key] = pm;
        return pm;
    };

    auto abrir = [&](const std::string& caminho) -> bool {
        if (!LoadScene(scene, caminho, cfg.extraRoots)) {
            aviso("nao consegui abrir: " + scene.status, 8.0f);
            return false;
        }
        doc = Doc();
        doc.elems.reserve(scene.gbin.elements.size());
        for (int i = 0; i < (int)scene.gbin.elements.size(); i++) {
            const GElement& g = scene.gbin.elements[i];
            EdElem e;
            e.orig = i;
            e.modelo = g.name;
            for (int k = 0; k < 9; k++) e.baseM[k] = g.matrixWorld[k];
            e.pos = Vector3{ g.matrixWorld[9], g.matrixWorld[10], g.matrixWorld[11] };
            e.animFlag = g.animFlag;
            e.collFlag = g.collFlag;
            e.courseType = g.courseType;
            e.script = g.script;
            e.portal = !g.script.empty();
            e.aabbArquivo = g.minMax;
            e.modelo3d = modeloDe(g.name);
            doc.elems.push_back(e);
        }
        hist.Reset(doc);
        cat.Descarrega();
        cat.Monta(scene.db, scene.gbin);
        sel = selCaixa = plantando = -1;
        modo = Modo::Navegar;
        arquivoProjeto.clear();

        // camera atras do tee, olhando pro pino (igual o SIM)
        if (!scene.teePoints.empty()) {
            Vector3 t = scene.teePoints[0];
            Vector3 alvo = scene.pinPoints.empty()
                ? Vector3Add(t, Vector3{ 0,0,-500 }) : scene.pinPoints[0];
            Vector3 d = Vector3Subtract(alvo, t); d.y = 0;
            float L = Vector3Length(d);
            if (L > 1.0f) d = Vector3Scale(d, 1.0f / L); else d = Vector3{ 0,0,-1 };
            cam.pos = Vector3Add(Vector3Subtract(t, Vector3Scale(d, 150.0f)), Vector3{ 0, 105, 0 });
            cam.LookAt(Vector3Add(t, Vector3Scale(d, 400.0f)));
        } else if (!scene.instances.empty()) {
            cam.pos = Vector3Add(scene.instances[0].worldCenter,
                                 Vector3{ 0, scene.instances[0].worldRadius * 0.5f + 50,
                                          scene.instances[0].worldRadius });
            cam.LookAt(scene.instances[0].worldCenter);
        }
        aviso(PathFileName(caminho) + "  |  " + std::to_string(doc.elems.size()) + " elementos", 5.0f);
        return true;
    };

    if (!cmdGbin.empty())      abrir(cmdGbin);
    else if (!cfg.gbin.empty()) abrir(cfg.gbin);

    if (modoShot) {
        if (temShotCam)  cam.pos = shotCam;
        if (temShotLook) cam.LookAt(shotLook);
        if (!shotBusca.empty()) cat.busca = shotBusca;
        if (shotPlantar >= 0) {
            std::vector<int> vis = cat.Filtrados();
            if (shotPlantar < (int)vis.size()) {
                plantando = vis[shotPlantar];
                modo = Modo::Plantar;
            }
        }
    }

    // ------------------------------------------------------------- loop -----
    while (!WindowShouldClose()) {
        const float dt = GetFrameTime();
        const int   sw = GetScreenWidth(), sh = GetScreenHeight();
        const bool  ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        const bool  shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);

        const Rectangle rCat = { (float)sw - 400.0f - 10.0f, 96.0f, 400.0f, (float)sh - 96.0f - 150.0f };
        if (modoShot) SetMousePosition(sw / 2, sh / 2 + 140);   // mira no chao
        const bool mouseNaUi = mostraCatalogo && CheckCollisionPointRec(GetMousePosition(), rCat);

        // ---------------------------------------------------------- camera --
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            if (!IsCursorHidden()) DisableCursor();
            Vector2 d = GetMouseDelta();
            cam.yaw   += d.x * 0.0032f;
            cam.pitch -= d.y * 0.0032f;
            const float lim = 1.55f;
            if (cam.pitch >  lim) cam.pitch =  lim;
            if (cam.pitch < -lim) cam.pitch = -lim;
        } else if (IsCursorHidden()) {
            EnableCursor();
        }

        if (!editandoTexto) {
            float v = cam.speed * (shift ? 3.0f : 1.0f) * dt;
            Vector3 f = cam.Forward(), r = cam.Right();
            if (IsKeyDown(KEY_W)) cam.pos = Vector3Add(cam.pos, Vector3Scale(f, v));
            if (IsKeyDown(KEY_S)) cam.pos = Vector3Subtract(cam.pos, Vector3Scale(f, v));
            if (IsKeyDown(KEY_D)) cam.pos = Vector3Add(cam.pos, Vector3Scale(r, v));
            if (IsKeyDown(KEY_A)) cam.pos = Vector3Subtract(cam.pos, Vector3Scale(r, v));
            if (IsKeyDown(KEY_SPACE)) cam.pos.y += v;
            if (IsKeyDown(KEY_LEFT_ALT)) cam.pos.y -= v;
        }
        if (!mouseNaUi) {
            float w = GetMouseWheelMove();
            if (w != 0 && !IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
                cam.speed *= (w > 0 ? 1.15f : 0.87f);
                if (cam.speed < 10) cam.speed = 10;
                if (cam.speed > 5000) cam.speed = 5000;
            }
        }

        Camera3D c3d = cam.ToCamera();

        // ------------------------------------------------- raio do mouse ----
        Ray raio = GetScreenToWorldRay(GetMousePosition(), c3d);
        temGhost = false;
        if (scene.loaded && !mouseNaUi && (modo != Modo::Navegar || arrastando)) {
            Vector3 h;
            if (RaioNoChao(scene, raio, h)) { ghost = h; temGhost = true; }
        }

        // ------------------------------------------------------- atalhos ----
        if (!editandoTexto) {
            if (ctrl && IsKeyPressed(KEY_O)) {
                std::string p = OpenFileDialog("Abrir hole (.gbin)",
                    "Mapa do PangYa (*.gbin)\0*.gbin\0Todos (*.*)\0*.*\0\0",
                    scene.loaded ? PathDir(scene.gbinPath).c_str() : NULL);
                if (!p.empty()) abrir(p);
            }
            if (ctrl && IsKeyPressed(KEY_Z) && hist.PodeDesfazer()) {
                doc = hist.Desfaz(); sel = selCaixa = -1;
                aviso("desfeito");
            }
            if (ctrl && IsKeyPressed(KEY_Y) && hist.PodeRefazer()) {
                doc = hist.Refaz(); sel = selCaixa = -1;
                aviso("refeito");
            }
            if (IsKeyPressed(KEY_P)) mostraCatalogo = !mostraCatalogo;
            if (IsKeyPressed(KEY_F1)) mostraAjuda = !mostraAjuda;
            if (IsKeyPressed(KEY_M)) mostraMarcadores = !mostraMarcadores;
            if (IsKeyPressed(KEY_K)) mostraCaixas = !mostraCaixas;
            if (IsKeyPressed(KEY_F)) fogOn = !fogOn;
            if (IsKeyPressed(KEY_L)) mostraSky = !mostraSky;
            if (IsKeyPressed(KEY_N) && scene.loaded) {
                modo = (modo == Modo::PorCaixa) ? Modo::Navegar : Modo::PorCaixa;
                plantando = -1;
                aviso(modo == Modo::PorCaixa
                      ? "modo soundbox: clique no chao pra por a caixa de spawn"
                      : "modo navegar");
            }
            if (IsKeyPressed(KEY_ESCAPE)) {
                if (modo != Modo::Navegar) { modo = Modo::Navegar; plantando = -1; }
                else { sel = selCaixa = -1; }
                arrastando = false;
            }
        }

        // Planta o objeto do catalogo no ponto do chao sob o cursor.
        // E lambda porque o modo de auto-teste (--auto) chama exatamente esta
        // mesma funcao: se fosse codigo duplicado, o teste nao provaria nada.
        auto plantaAqui = [&]() {
            EdElem e;
            e.orig = -1;
            e.modelo = cat.itens[plantando].nome;
            e.pos = WorldToPangya(ghost);
            e.pos.y += alturaExtra;
            e.giro = novoGiro;
            e.escala = novaEscala;
            e.tocado = true;
            e.portal = comoPortal;
            if (comoPortal) {
                e.script = "60 70";     // o valor dos 15 portais do Wiz City
                e.animFlag = 4;         // self animate: o anel gira
                e.collFlag = 0;         // atravessavel
                e.pos.y += 90.0f;       // centro do aro fica ~90 acima do chao
            }
            e.modelo3d = modeloDe(e.modelo);
            doc.elems.push_back(e);
            sel = (int)doc.elems.size() - 1;
            selCaixa = -1;
            hist.Marca(doc);
            aviso("plantado: " + e.modelo);
        };

        // -------------------------------------------------- clique no 3D ----
        if (scene.loaded && !mouseNaUi && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (modo == Modo::Plantar && plantando >= 0 && temGhost) {
                plantaAqui();
                if (!shift) { modo = Modo::Navegar; plantando = -1; }
            } else if (modo == Modo::PorCaixa && temGhost) {
                EdCaixa c;
                c.pos = WorldToPangya(ghost);
                c.pos.y += 60.0f;
                doc.caixas.push_back(c);
                selCaixa = (int)doc.caixas.size() - 1;
                sel = -1;
                mostraCaixas = true;
                hist.Marca(doc);
                modo = Modo::Navegar;
                aviso("soundbox posta -- edite o script no painel de baixo");
            } else if (modo == Modo::Navegar) {
                // seleciona o mais proximo sob o cursor
                float melhor = 1e30f;
                int achou = -1;
                for (int i = 0; i < (int)doc.elems.size(); i++) {
                    if (doc.elems[i].apagado) continue;
                    RayCollision rc = GetRayCollisionBox(raio, CaixaMundo(doc.elems[i]));
                    if (rc.hit && rc.distance < melhor) { melhor = rc.distance; achou = i; }
                }
                int achouCaixa = -1;
                if (mostraCaixas) {
                    for (int i = 0; i < (int)doc.caixas.size(); i++) {
                        Vector3 p = PangyaToWorld(doc.caixas[i].pos.x, doc.caixas[i].pos.y, doc.caixas[i].pos.z);
                        BoundingBox bb;
                        const float r = doc.caixas[i].raio;
                        bb.min = Vector3{ p.x - r, p.y - r, p.z - r };
                        bb.max = Vector3{ p.x + r, p.y + r, p.z + r };
                        RayCollision rc = GetRayCollisionBox(raio, bb);
                        if (rc.hit && rc.distance < melhor) { melhor = rc.distance; achouCaixa = i; achou = -1; }
                    }
                }
                sel = achou; selCaixa = achouCaixa;
                campoAtivo = 0; editandoTexto = false;
            }
        }

        // ------------------------------------------- editar o selecionado ---
        if (sel >= 0 && sel < (int)doc.elems.size() && !editandoTexto) {
            EdElem& e = doc.elems[sel];
            bool mudou = false;
            const float passo = shift ? 1.0f : 10.0f;
            const float dGiro = (shift ? 1.0f : 5.0f) * DEG2RAD;

            if (IsKeyPressed(KEY_G)) { arrastando = !arrastando; }
            if (arrastando && temGhost) {
                Vector3 np = WorldToPangya(ghost);
                np.y += alturaExtra;
                if (fabsf(np.x - e.pos.x) > 0.001f || fabsf(np.z - e.pos.z) > 0.001f) {
                    e.pos = np; e.tocado = true;
                }
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    arrastando = false; hist.Marca(doc); aviso("movido");
                }
            }
            if (IsKeyPressed(KEY_Q) || IsKeyPressedRepeat(KEY_Q)) { e.giro -= dGiro; mudou = true; }
            if (IsKeyPressed(KEY_E) || IsKeyPressedRepeat(KEY_E)) { e.giro += dGiro; mudou = true; }
            if (IsKeyPressed(KEY_LEFT_BRACKET)  || IsKeyPressedRepeat(KEY_LEFT_BRACKET))  { e.escala *= shift ? 0.99f : 0.94f; mudou = true; }
            if (IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressedRepeat(KEY_RIGHT_BRACKET)) { e.escala *= shift ? 1.01f : 1.06f; mudou = true; }
            if (IsKeyPressed(KEY_UP)    || IsKeyPressedRepeat(KEY_UP))    { e.pos.z += passo; mudou = true; }
            if (IsKeyPressed(KEY_DOWN)  || IsKeyPressedRepeat(KEY_DOWN))  { e.pos.z -= passo; mudou = true; }
            if (IsKeyPressed(KEY_LEFT)  || IsKeyPressedRepeat(KEY_LEFT))  { e.pos.x -= passo; mudou = true; }
            if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) { e.pos.x += passo; mudou = true; }
            if (IsKeyPressed(KEY_PAGE_UP)   || IsKeyPressedRepeat(KEY_PAGE_UP))   { e.pos.y += passo; mudou = true; }
            if (IsKeyPressed(KEY_PAGE_DOWN) || IsKeyPressedRepeat(KEY_PAGE_DOWN)) { e.pos.y -= passo; mudou = true; }
            if (IsKeyPressed(KEY_HOME)) {            // assenta no terreno
                Vector3 w = PangyaToWorld(e.pos.x, e.pos.y, e.pos.z);
                float y;
                if (scene.ground.HeightAt(w.x, w.z, y)) { e.pos.y = y; mudou = true; }
            }
            if (e.escala < 0.02f) e.escala = 0.02f;
            if (e.escala > 50.0f) e.escala = 50.0f;

            if (IsKeyPressed(KEY_DELETE)) {
                e.apagado = true; e.tocado = true;
                sel = -1; arrastando = false;
                hist.Marca(doc);
                aviso("apagado (Ctrl+Z desfaz)");
            } else if (IsKeyPressed(KEY_C) && ctrl) {
                EdElem n = e;
                n.orig = -1; n.tocado = true; n.apagado = false;
                n.pos.x += 25.0f;
                doc.elems.push_back(n);
                sel = (int)doc.elems.size() - 1;
                hist.Marca(doc);
                aviso("duplicado");
            } else if (mudou) {
                e.tocado = true;
                // agrupa as teclinhas num unico passo de historico enquanto
                // o usuario segura; marca quando solta
            }
            if (mudou && !IsKeyDown(KEY_Q) && !IsKeyDown(KEY_E) &&
                !IsKeyDown(KEY_LEFT_BRACKET) && !IsKeyDown(KEY_RIGHT_BRACKET) &&
                !IsKeyDown(KEY_UP) && !IsKeyDown(KEY_DOWN) && !IsKeyDown(KEY_LEFT) &&
                !IsKeyDown(KEY_RIGHT) && !IsKeyDown(KEY_PAGE_UP) && !IsKeyDown(KEY_PAGE_DOWN))
                hist.Marca(doc);
        }
        if (selCaixa >= 0 && selCaixa < (int)doc.caixas.size() && !editandoTexto) {
            EdCaixa& c = doc.caixas[selCaixa];
            if (IsKeyPressed(KEY_LEFT_BRACKET))  { c.raio *= 0.9f;  hist.Marca(doc); }
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) { c.raio *= 1.11f; hist.Marca(doc); }
            if (IsKeyPressed(KEY_DELETE)) {
                doc.caixas.erase(doc.caixas.begin() + selCaixa);
                selCaixa = -1; hist.Marca(doc); aviso("soundbox apagada");
            }
        }

        // =================================================== DESENHO ========
        // ---- gerar o .gbin (pedido no quadro anterior pelo F5 / botao) -----
        if (gerarPendente) {
            gerarPendente = false;
            const std::string dirApp = GetApplicationDirectory();
            std::string saida = dirApp + std::string("saida");
            if (!DirectoryExists(saida.c_str())) MakeDirectory(saida.c_str());
            std::string cmd = "python \"" + dirApp + "tools\\aplicar.py\" \""
                            + arquivoProjeto + "\" --saida \"" + saida + "\" --verboso";
            std::string out;
            int rc = -1;
            RunAndWait(cmd, dirApp, out, rc);

            const std::string log = dirApp + std::string("ultimo_gerar.log");
            FILE* lf = fopen(log.c_str(), "w");
            if (lf) {
                fputs(cmd.c_str(), lf);
                fprintf(lf, "\n\nrc=%d\n\n", rc);
                fputs(out.c_str(), lf);
                fclose(lf);
            }
            if (rc == 0) aviso("gbin gerado em " + saida, 12.0f);
            else if (rc < 0) aviso("nao consegui rodar o python -- ele esta no PATH? (log: ultimo_gerar.log)", 14.0f);
            else aviso("o aplicar.py falhou (rc=" + std::to_string(rc) + ") -- veja ultimo_gerar.log", 14.0f);
        }

        // miniaturas pendentes (fora do BeginDrawing -- ver o comentario no
        // Catalogo). Poucas por quadro pra nao engasgar.
        for (int feitos = 0; feitos < 3 && !cat.fila.empty(); feitos++) {
            const int i = cat.fila.front();
            cat.fila.erase(cat.fila.begin());
            if (i < 0 || i >= (int)cat.itens.size()) continue;
            cat.itens[i].thumb = RenderPieceThumb(cat.itens[i].caminho, scene.db, scene.tex,
                                                  fboThumb, vs.sh, mat,
                                                  vs.locAlphaCut, vs.locFogColor);
        }

        const Color bg = scene.hasFog
            ? Color{ (unsigned char)(scene.fogR * 255), (unsigned char)(scene.fogG * 255),
                     (unsigned char)(scene.fogB * 255), 255 }
            : Color{ 120, 170, 220, 255 };

        BeginDrawing();
        ClearBackground(bg);

        if (scene.loaded) {
            BeginMode3D(c3d);
            rlDisableBackfaceCulling();
            vs.SetCam(cam.pos);
            if (mostraSky) DrawSky(scene, cam, vs, mat, 30000.0f);
            vs.SetFog(scene.fogR, scene.fogG, scene.fogB, fogOn && scene.hasFog);
            vs.SetFogRange(scene.fogNear, scene.fogFar);

            // terreno (instances[0] quando o gbin tem base)
            vs.SetCut(0.5f);
            if (scene.gbin.hasBase && !scene.instances.empty()) {
                const Instance& in = scene.instances[0];
                for (const auto& s : in.model->subs) {
                    if (s.blend) continue;
                    mat.maps[MATERIAL_MAP_DIFFUSE].texture = s.tex;
                    DrawMesh(s.mesh, mat, in.xf);
                }
            }
            // elementos opacos
            for (const auto& e : doc.elems) {
                if (e.apagado || !e.modelo3d || !e.modelo3d->ok) continue;
                const Matrix xf = MatrizRender(e);
                for (const auto& s : e.modelo3d->subs) {
                    if (s.blend) continue;
                    mat.maps[MATERIAL_MAP_DIFFUSE].texture = s.tex;
                    DrawMesh(s.mesh, mat, xf);
                }
            }
            // transparencias
            vs.SetCut(0.01f);
            BeginBlendMode(BLEND_ALPHA);
            rlDisableDepthMask();
            if (scene.gbin.hasBase && !scene.instances.empty()) {
                const Instance& in = scene.instances[0];
                for (const auto& s : in.model->subs) {
                    if (!s.blend) continue;
                    mat.maps[MATERIAL_MAP_DIFFUSE].texture = s.tex;
                    DrawMesh(s.mesh, mat, in.xf);
                }
            }
            for (const auto& e : doc.elems) {
                if (e.apagado || !e.modelo3d || !e.modelo3d->ok) continue;
                const Matrix xf = MatrizRender(e);
                for (const auto& s : e.modelo3d->subs) {
                    if (!s.blend) continue;
                    mat.maps[MATERIAL_MAP_DIFFUSE].texture = s.tex;
                    DrawMesh(s.mesh, mat, xf);
                }
            }

            // fantasma do objeto que esta sendo plantado
            if (modo == Modo::Plantar && plantando >= 0 && temGhost) {
                PetModel* pm = modeloDe(cat.itens[plantando].nome);
                if (pm && pm->ok) {
                    EdElem tmp;
                    tmp.pos = WorldToPangya(ghost);
                    tmp.pos.y += alturaExtra + (comoPortal ? 90.0f : 0.0f);
                    tmp.giro = novoGiro;
                    tmp.escala = novaEscala;
                    const Matrix xf = MatrizRender(tmp);
                    mat.maps[MATERIAL_MAP_DIFFUSE].color = Color{ 140, 255, 170, 165 };
                    for (const auto& s : pm->subs) {
                        mat.maps[MATERIAL_MAP_DIFFUSE].texture = s.tex;
                        DrawMesh(s.mesh, mat, xf);
                    }
                    mat.maps[MATERIAL_MAP_DIFFUSE].color = WHITE;
                }
            }
            rlEnableDepthMask();
            EndBlendMode();

            // ---- gizmos ----
            if (temGhost) {
                DrawCircle3D(Vector3Add(ghost, Vector3{ 0, 0.5f, 0 }), 14.0f,
                             Vector3{ 1,0,0 }, 90.0f, Color{ 120, 255, 160, 220 });
                DrawLine3D(ghost, Vector3Add(ghost, Vector3{ 0, 60, 0 }), Color{ 120, 255, 160, 150 });
            }
            if (sel >= 0 && sel < (int)doc.elems.size() && !doc.elems[sel].apagado) {
                const EdElem& e = doc.elems[sel];
                BoundingBox bb = CaixaMundo(e);
                DrawBoundingBox(bb, Color{ 255, 210, 60, 255 });
                Vector3 p = PangyaToWorld(e.pos.x, e.pos.y, e.pos.z);
                // seta mostrando o eixo Z do elemento (pra onde o portal empurra)
                float m[12]; MatrizDe(e, m);
                Vector3 z = PangyaToWorld(m[6], m[7], m[8]);
                z = Vector3Scale(Vector3Normalize(z), 70.0f);
                DrawLine3D(p, Vector3Add(p, z), Color{ 90, 200, 255, 255 });
                DrawSphere(Vector3Add(p, z), 3.0f, Color{ 90, 200, 255, 255 });
                DrawLine3D(p, Vector3Add(p, Vector3{ 0, 100, 0 }), Color{ 255, 210, 60, 200 });
            }
            if (mostraCaixas) {
                for (int i = 0; i < (int)doc.caixas.size(); i++) {
                    const EdCaixa& c = doc.caixas[i];
                    Vector3 p = PangyaToWorld(c.pos.x, c.pos.y, c.pos.z);
                    const Color col = (i == selCaixa) ? Color{ 255, 160, 40, 255 }
                                                      : Color{ 120, 220, 255, 190 };
                    DrawCubeWires(p, c.raio * 2, c.raio * 2, c.raio * 2, col);
                }
                for (const auto& sb : scene.gbin.soundBoxes) {
                    Vector3 a = PangyaToWorld(sb.box.minx, sb.box.miny, sb.box.minz);
                    Vector3 b = PangyaToWorld(sb.box.maxx, sb.box.maxy, sb.box.maxz);
                    Vector3 ctr = Vector3Scale(Vector3Add(a, b), 0.5f);
                    DrawCubeWires(ctr, fabsf(b.x - a.x), fabsf(b.y - a.y), fabsf(b.z - a.z),
                                  Color{ 150, 150, 170, 110 });
                }
            }
            if (mostraMarcadores) {
                rlDrawRenderBatchActive();
                rlDisableDepthTest();
                for (const auto& mk : scene.markers) {
                    DrawSphere(mk.pos, 1.2f, mk.color);
                    DrawLine3D(mk.pos, Vector3Add(mk.pos, Vector3{ 0, 90, 0 }), mk.color);
                }
                rlDrawRenderBatchActive();
                rlEnableDepthTest();
            }
            rlEnableBackfaceCulling();
            EndMode3D();

            if (mostraMarcadores) {
                for (const auto& mk : scene.markers) {
                    Vector3 top = Vector3Add(mk.pos, Vector3{ 0, 95, 0 });
                    if (Vector3DotProduct(Vector3Subtract(top, cam.pos), cam.Forward()) <= 0) continue;
                    Vector2 s = GetWorldToScreen(top, c3d);
                    if (s.x < -100 || s.y < -100 || s.x > sw + 100 || s.y > sh + 100) continue;
                    int w = MeasureText(mk.label, 16);
                    DrawRectangle((int)s.x - w / 2 - 4, (int)s.y - 10, w + 8, 20, Color{ 0,0,0,150 });
                    DrawText(mk.label, (int)s.x - w / 2, (int)s.y - 8, 16, mk.color);
                }
            }
        } else {
            const char* m1 = "Nenhum hole carregado";
            const char* m2 = "Ctrl+O pra abrir um .gbin   (ou arraste o arquivo pra ca)";
            DrawText(m1, sw / 2 - MeasureText(m1, 30) / 2, sh / 2 - 40, 30, RAYWHITE);
            DrawText(m2, sw / 2 - MeasureText(m2, 18) / 2, sh / 2 + 4, 18, Color{ 180, 200, 225, 255 });
        }

        // ------------------------------------------------------- arrastar ---
        if (IsFileDropped()) {
            FilePathList fl = LoadDroppedFiles();
            if (fl.count > 0) {
                std::string p = fl.paths[0];
                if (ToLower(p).size() > 5 && ToLower(p).substr(ToLower(p).size() - 5) == ".gbin")
                    abrir(p);
                else { scene.db.AddRoot(p); cat.Monta(scene.db, scene.gbin); aviso("pasta indexada: " + p); }
            }
            UnloadDroppedFiles(fl);
        }

        // ============================== HUD =================================
        int novos = 0, movidos = 0, apagados = 0;
        for (const auto& e : doc.elems) {
            if (e.orig < 0 && !e.apagado) novos++;
            else if (e.orig >= 0 && e.apagado) apagados++;
            else if (e.orig >= 0 && e.tocado) movidos++;
        }

        DrawRectangle(0, 0, sw, 90, Color{ 12, 15, 20, 218 });
        {
            char b[512];
            snprintf(b, sizeof(b), "%s", scene.loaded ? PathFileName(scene.gbinPath).c_str()
                                                      : "(nenhum hole)");
            DrawText(b, 12, 8, 22, Color{ 255, 210, 90, 255 });
            snprintf(b, sizeof(b), "PAR %d   |   %d elementos   |   +%d novos   ~%d movidos   -%d apagados   |   %d soundbox novas",
                     scene.gbin.hasMapCheck ? scene.gbin.mapCheck.parHole : 0,
                     (int)doc.elems.size(), novos, movidos, apagados, (int)doc.caixas.size());
            DrawText(b, 12, 36, 16, RAYWHITE);
            Vector3 pg = WorldToPangya(cam.pos);
            snprintf(b, sizeof(b), "camera X=%.1f Y=%.1f Z=%.1f   vel=%.0f/s   |   %d arquivos indexados   |   F1 = ajuda",
                     pg.x, pg.y, pg.z, cam.speed, (int)scene.db.FileCount());
            DrawText(b, 12, 60, 15, Color{ 150, 172, 200, 255 });
            if (temGhost) {
                Vector3 gp = WorldToPangya(ghost);
                snprintf(b, sizeof(b), "cursor no chao  X=%.1f  Y=%.1f  Z=%.1f", gp.x, gp.y, gp.z);
                DrawText(b, sw - MeasureText(b, 15) - 14, 56, 15, Color{ 130, 235, 170, 255 });
            }
        }
        {
            const char* m = IsCursorHidden() ? "olhando (solte o botao direito)"
                                             : "segure o BOTAO DIREITO pra olhar";
            int w = MeasureText(m, 15);
            DrawText(m, sw - w - 14, 10, 15, Color{ 255, 200, 120, 255 });
            char fps[64]; snprintf(fps, sizeof(fps), "%d FPS", GetFPS());
            int wf = MeasureText(fps, 15);
            DrawText(fps, sw - wf - 14, 30, 15, Color{ 120, 230, 140, 255 });
        }
        if (modo == Modo::Plantar && plantando >= 0) {
            char b[256];
            snprintf(b, sizeof(b), "PLANTANDO  %s   -   clique no chao   (Shift+clique = varios, ESC cancela)",
                     cat.itens[plantando].nome.c_str());
            int w = MeasureText(b, 17);
            DrawRectangle(sw / 2 - w / 2 - 12, 98, w + 24, 30, Color{ 20, 80, 45, 235 });
            DrawText(b, sw / 2 - w / 2, 104, 17, Color{ 170, 255, 190, 255 });
        } else if (modo == Modo::PorCaixa) {
            const char* b = "SOUNDBOX  -  clique no chao pra por a caixa de spawn de NPC   (ESC cancela)";
            int w = MeasureText(b, 17);
            DrawRectangle(sw / 2 - w / 2 - 12, 98, w + 24, 30, Color{ 25, 60, 95, 235 });
            DrawText(b, sw / 2 - w / 2, 104, 17, Color{ 170, 220, 255, 255 });
        } else if (arrastando) {
            const char* b = "MOVENDO  -  mexa o mouse e clique pra soltar   (G ou ESC cancela)";
            int w = MeasureText(b, 17);
            DrawRectangle(sw / 2 - w / 2 - 12, 98, w + 24, 30, Color{ 95, 70, 20, 235 });
            DrawText(b, sw / 2 - w / 2, 104, 17, Color{ 255, 220, 150, 255 });
        }

        // ====================== PAINEL DE PROPRIEDADES ======================
        bool travaTexto = false;
        if (sel >= 0 && sel < (int)doc.elems.size()) {
            EdElem& e = doc.elems[sel];
            const Rectangle r = { 10, (float)sh - 208, 470, 168 };
            Painel(r, "objeto selecionado");
            int x = (int)r.x + 12, y = (int)r.y + 34;
            char b[256];
            Linha(x, y, "modelo", e.modelo.c_str(), Color{ 255, 220, 120, 255 });
            snprintf(b, sizeof(b), "%s", e.orig < 0 ? "NOVO" : (e.tocado ? "original (mexido)" : "original"));
            Linha(x, y, "origem", b, e.orig < 0 ? Color{ 130, 255, 160, 255 } : Color{ 190, 205, 225, 255 });
            snprintf(b, sizeof(b), "X %.1f   Y %.1f   Z %.1f", e.pos.x, e.pos.y, e.pos.z);
            Linha(x, y, "posicao", b);
            snprintf(b, sizeof(b), "%.1f graus", e.giro * RAD2DEG);
            Linha(x, y, "giro (Q/E)", b);
            snprintf(b, sizeof(b), "%.3f x", e.escala);
            Linha(x, y, "escala ([ ])", b);
            snprintf(b, sizeof(b), "anim=%d  coll=%d  course=%d", e.animFlag, e.collFlag, e.courseType);
            Linha(x, y, "flags", b);

            if (e.portal || !e.script.empty()) {
                DrawText("script (2 floats: sobe / avanca)", x, y, 13, Color{ 140, 156, 178, 255 });
                Rectangle rs = { (float)x + 220, (float)y - 4, 150, 22 };
                bool ativo = (campoAtivo == 2);
                if (CheckCollisionPointRec(GetMousePosition(), rs) &&
                    IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { campoAtivo = 2; ativo = true; }
                if (CampoTexto(rs, e.script, ativo, "60 70")) e.tocado = true;
                if (ativo) travaTexto = true;
            }
            DrawText("G mover   Del apagar   Ctrl+C duplicar   Home assentar no chao",
                     x, (int)r.y + (int)r.height - 20, 13, Color{ 130, 148, 172, 255 });
        } else if (selCaixa >= 0 && selCaixa < (int)doc.caixas.size()) {
            EdCaixa& c = doc.caixas[selCaixa];
            const Rectangle r = { 10, (float)sh - 208, 560, 168 };
            Painel(r, "soundbox (spawn de NPC)");
            int x = (int)r.x + 12, y = (int)r.y + 34;
            char b[256];
            snprintf(b, sizeof(b), "X %.1f   Y %.1f   Z %.1f", c.pos.x, c.pos.y, c.pos.z);
            Linha(x, y, "centro", b);
            snprintf(b, sizeof(b), "%.0f  ([ e ] mudam)", c.raio * 2);
            Linha(x, y, "aresta", b);
            snprintf(b, sizeof(b), "%d", c.tipo);
            Linha(x, y, "tipo", b);
            DrawText("script do spawn", x, y + 4, 13, Color{ 140, 156, 178, 255 });
            Rectangle rs = { (float)x, (float)y + 22, r.width - 24, 24 };
            bool ativo = (campoAtivo == 3);
            if (CheckCollisionPointRec(GetMousePosition(), rs) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { campoAtivo = 3; ativo = true; }
            CampoTexto(rs, c.nome, ativo, "*type 0 *pet NPC_SeaGull.pet *num 5");
            if (ativo) travaTexto = true;
            DrawText("Del apaga a caixa", x, (int)r.y + (int)r.height - 20, 13,
                     Color{ 130, 148, 172, 255 });
        }

        // ============================ CATALOGO ==============================
        if (mostraCatalogo && scene.loaded) {
            Painel(rCat, "catalogo de objetos  (P esconde)");
            const int x = (int)rCat.x + 10;
            int y = (int)rCat.y + 34;

            Rectangle rb = { (float)x, (float)y, rCat.width - 20 - 96, 26 };
            bool ativoBusca = (campoAtivo == 1);
            if (CheckCollisionPointRec(GetMousePosition(), rb) &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { campoAtivo = 1; ativoBusca = true; }
            if (CampoTexto(rb, cat.busca, ativoBusca, "buscar...")) cat.scroll = 0;
            if (ativoBusca) travaTexto = true;

            Rectangle rf = { rb.x + rb.width + 6, (float)y, 90, 26 };
            if (UiButton(rf, cat.soUsados ? "so do mapa" : "todos", cat.soUsados,
                         IsMouseButtonPressed(MOUSE_BUTTON_LEFT), 13)) {
                cat.soUsados = !cat.soUsados; cat.scroll = 0;
            }
            y += 32;

            // opcoes do que sera plantado
            Rectangle ro = { (float)x, (float)y, rCat.width - 20, 26 };
            {
                char b[96];
                Rectangle a = ro; a.width = (ro.width - 12) / 3;
                snprintf(b, sizeof(b), "giro %.0f", novoGiro * RAD2DEG);
                if (UiButton(a, b, false, IsMouseButtonPressed(MOUSE_BUTTON_LEFT), 13))
                    novoGiro += (shift ? -15.0f : 15.0f) * DEG2RAD;
                a.x += a.width + 6;
                snprintf(b, sizeof(b), "escala %.2f", novaEscala);
                if (UiButton(a, b, false, IsMouseButtonPressed(MOUSE_BUTTON_LEFT), 13))
                    novaEscala *= (shift ? 0.9f : 1.1f);
                a.x += a.width + 6;
                if (UiButton(a, comoPortal ? "PORTAL" : "objeto", comoPortal,
                             IsMouseButtonPressed(MOUSE_BUTTON_LEFT), 13))
                    comoPortal = !comoPortal;
            }
            y += 32;

            const Rectangle rl = { (float)x, (float)y, rCat.width - 20,
                                   rCat.y + rCat.height - y - 12 };
            DrawRectangleRec(rl, Color{ 12, 14, 19, 200 });
            BeginScissorMode((int)rl.x, (int)rl.y, (int)rl.width, (int)rl.height);

            std::vector<int> vis = cat.Filtrados();
            const int alt = 44;
            const int cabem = (int)(rl.height / alt) + 1;
            if (CheckCollisionPointRec(GetMousePosition(), rl)) {
                float w = GetMouseWheelMove();
                if (w != 0) cat.scroll -= (int)w * 3;
            }
            int maxScroll = (int)vis.size() - (int)(rl.height / alt);
            if (maxScroll < 0) maxScroll = 0;
            if (cat.scroll > maxScroll) cat.scroll = maxScroll;
            if (cat.scroll < 0) cat.scroll = 0;

            for (int k = 0; k < cabem; k++) {
                const int idx = cat.scroll + k;
                if (idx < 0 || idx >= (int)vis.size()) continue;
                ItemCat& it = cat.itens[vis[idx]];
                Rectangle ri = { rl.x, rl.y + k * (float)alt, rl.width, (float)alt - 2 };
                const bool hover = CheckCollisionPointRec(GetMousePosition(), ri);
                const bool selecionado = (plantando == vis[idx]);
                DrawRectangleRec(ri, selecionado ? Color{ 34, 88, 58, 235 }
                                   : hover ? Color{ 40, 48, 62, 235 } : Color{ 20, 24, 32, 180 });

                if (!it.pediuThumb) {
                    it.pediuThumb = true;
                    cat.fila.push_back(vis[idx]);   // gera no comeco do proximo quadro
                }
                if (it.thumb.id)
                    DrawTexturePro(it.thumb, Rectangle{ 0,0,(float)it.thumb.width,(float)it.thumb.height },
                                   Rectangle{ ri.x + 3, ri.y + 3, 36, 36 }, Vector2{ 0,0 }, 0, WHITE);
                else
                    DrawRectangle((int)ri.x + 3, (int)ri.y + 3, 36, 36, Color{ 30, 34, 44, 255 });

                DrawText(it.nome.c_str(), (int)ri.x + 46, (int)ri.y + 6, 14,
                         selecionado ? RAYWHITE : Color{ 205, 218, 236, 255 });
                if (it.usos > 0) {
                    char b[48]; snprintf(b, sizeof(b), "ja usado %dx neste hole", it.usos);
                    DrawText(b, (int)ri.x + 46, (int)ri.y + 24, 12, Color{ 120, 200, 255, 220 });
                }
                if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    plantando = vis[idx];
                    modo = Modo::Plantar;
                    campoAtivo = 0;
                    aviso("clique no chao pra plantar  " + it.nome);
                }
            }
            EndScissorMode();
            DrawRectangleLinesEx(rl, 1, Color{ 55, 66, 84, 255 });
            {
                char b[128];
                snprintf(b, sizeof(b), "%d de %d modelos", (int)vis.size(), (int)cat.itens.size());
                DrawText(b, (int)rl.x + 2, (int)(rl.y + rl.height) + 2, 12, Color{ 120, 138, 162, 255 });
            }
        }

        // =============================== BOTOES =============================
        {
            const float bw = 128, bh = 30;
            float bx = (float)sw - bw - 12, by = (float)sh - 130;
            const bool clique = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

            if (UiButton(Rectangle{ bx, by, bw, bh }, "Abrir hole", false, clique)) {
                std::string p = OpenFileDialog("Abrir hole (.gbin)",
                    "Mapa do PangYa (*.gbin)\0*.gbin\0Todos (*.*)\0*.*\0\0",
                    scene.loaded ? PathDir(scene.gbinPath).c_str() : NULL);
                if (!p.empty()) abrir(p);
            }
            by += bh + 6;
            if (UiButton(Rectangle{ bx, by, bw, bh }, "Salvar projeto", false, clique) ||
                (ctrl && IsKeyPressed(KEY_S) && !editandoTexto)) {
                if (!scene.loaded) aviso("abra um hole primeiro");
                else {
                    if (arquivoProjeto.empty()) {
                        std::string dir = std::string(GetApplicationDirectory()) + "projetos";
                        if (!DirectoryExists(dir.c_str())) MakeDirectory(dir.c_str());
                        arquivoProjeto = dir + "\\" + PathStem(scene.gbinPath) + "_edicao.json";
                    }
                    if (SalvaJson(arquivoProjeto, scene.gbinPath, scene.db.Roots(), doc))
                        aviso("salvo: " + arquivoProjeto, 6.0f);
                    else
                        aviso("NAO consegui salvar em " + arquivoProjeto, 8.0f);
                }
            }
            by += bh + 6;
            if (UiButton(Rectangle{ bx, by, bw, bh }, "GERAR .gbin", true, clique) ||
                (IsKeyPressed(KEY_F5) && !editandoTexto)) {
                if (!scene.loaded) aviso("abra um hole primeiro");
                else {
                    const std::string dirApp = GetApplicationDirectory();
                    std::string dirProj = dirApp + std::string("projetos");
                    if (!DirectoryExists(dirProj.c_str())) MakeDirectory(dirProj.c_str());
                    if (arquivoProjeto.empty())
                        arquivoProjeto = dirProj + "\\" + PathStem(scene.gbinPath) + "_edicao.json";
                    if (!SalvaJson(arquivoProjeto, scene.gbinPath, scene.db.Roots(), doc)) {
                        aviso("nao consegui salvar o projeto", 8.0f);
                    } else {
                        // roda no comeco do proximo quadro: assim o aviso
                        // "gerando..." chega a aparecer, e o quadro atual
                        // fecha inteiro antes do processo travar a thread.
                        gerarPendente = true;
                        aviso("gerando o .gbin...", 30.0f);
                    }
                }
            }
        }

        // ============================== AJUDA ===============================
        if (mostraAjuda) {
            const int w = 700, h = 468;
            const int x = sw / 2 - w / 2, y = sh / 2 - h / 2;
            DrawRectangle(0, 0, sw, sh, Color{ 0,0,0,150 });
            Painel(Rectangle{ (float)x, (float)y, (float)w, (float)h }, "Ghost Map Editor - ajuda (F1 fecha)");
            const char* L[] = {
                "CAMERA",
                "  botao direito segurado ... olhar em volta",
                "  W A S D ................. andar     Space / Alt ... subir / descer",
                "  Shift ................... 3x mais rapido     roda do mouse ... velocidade",
                "",
                "OBJETOS",
                "  clique esquerdo ......... selecionar",
                "  no catalogo, clique num modelo e depois no chao pra PLANTAR",
                "  Shift+clique no chao .... planta varios sem sair do modo",
                "  G ....................... pega o selecionado e move com o mouse",
                "  setas ................... move em X/Z      PageUp/PageDown ... move em Y",
                "  Q / E ................... gira      [ / ] ... escala",
                "  Home .................... assenta no terreno",
                "  Ctrl+C .................. duplica       Del ... apaga",
                "  Shift junto ............. passo fino",
                "",
                "PORTAL (booster) e SOUNDBOX",
                "  botao PORTAL no catalogo: planta com script '60 70', anim=4, coll=0",
                "     e ja sobe 90 unidades (o centro do aro, como no Wiz City).",
                "     A seta azul do selecionado mostra pra ONDE o portal empurra.",
                "  N ....................... poe uma soundbox de spawn de NPC",
                "",
                "ARQUIVO",
                "  Ctrl+O abrir     Ctrl+S salvar projeto (.json)     F5 gerar o .gbin",
                "  Ctrl+Z desfazer  Ctrl+Y refazer",
                "  P catalogo   M marcadores   K soundboxes   F neblina   L ceu",
                "",
                "As lights de TEE e PIN e o bloco MapCheck NAO sao tocados pelo editor.",
            };
            int ly = y + 34;
            for (const char* l : L) {
                const bool tit = (l[0] != ' ' && l[0] != 0);
                DrawText(l, x + 16, ly, tit ? 15 : 14,
                         tit ? Color{ 255, 210, 90, 255 } : Color{ 205, 218, 236, 255 });
                ly += tit ? 19 : 16;
            }
        }

        // ============================ RODAPE ================================
        if (!msg.empty() && GetTime() < msgAte) {
            int w = MeasureText(msg.c_str(), 17);
            DrawRectangle(sw / 2 - w / 2 - 14, sh - 44, w + 28, 30, Color{ 0, 0, 0, 205 });
            DrawText(msg.c_str(), sw / 2 - w / 2, sh - 38, 17, Color{ 255, 225, 140, 255 });
        }
        {
            const int fs = 15;
            const int w = MeasureText(kCredito, fs);
            Rectangle rc = { (float)(sw - w - 20), (float)(sh - fs - 12), (float)(w + 16), (float)(fs + 9) };
            const bool hover = CheckCollisionPointRec(GetMousePosition(), rc) && !IsCursorHidden();
            DrawRectangleRec(rc, Color{ 0, 0, 0, hover ? (unsigned char)200 : (unsigned char)120 });
            DrawText(kCredito, (int)rc.x + 8, (int)rc.y + 4, fs,
                     hover ? Color{ 255, 230, 150, 255 } : Color{ 155, 185, 215, 220 });
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) OpenURL(kUrl);
        }

        editandoTexto = travaTexto;
        if (!travaTexto && campoAtivo != 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            campoAtivo = 0;

        EndDrawing();

        if (modoShot) {
            quadrosShot++;
            // no meio do caminho, o auto-teste: planta 3 objetos com giros
            // diferentes, apaga um original, move outro e grava o projeto.
            if (!shotAuto.empty() && quadrosShot == 20 &&
                modo == Modo::Plantar && plantando >= 0 && temGhost) {
                for (int k = 0; k < 3; k++) {
                    novoGiro = k * 0.7f;
                    novaEscala = 1.0f + k * 0.15f;
                    ghost.x += 60.0f;
                    plantaAqui();
                }
                if (doc.elems.size() > 12) {
                    doc.elems[7].apagado = true;  doc.elems[7].tocado = true;
                    doc.elems[9].pos.x += 40.0f;  doc.elems[9].tocado = true;
                    hist.Marca(doc);
                }
                if (SalvaJson(shotAuto, scene.gbinPath, scene.db.Roots(), doc)) {
                    aviso("auto-teste: projeto salvo em " + shotAuto, 30.0f);
                    // exercita tambem o botao GERAR (RunAndWait + aplicar.py)
                    arquivoProjeto = shotAuto;
                    gerarPendente = true;
                } else {
                    aviso("auto-teste: FALHOU ao salvar", 30.0f);
                }
            }
            // uns quadros de folga pra as miniaturas do catalogo aparecerem
            if (quadrosShot >= 40) {
                Image im = LoadImageFromScreen();
                ExportImage(im, shotOut.c_str());
                UnloadImage(im);
                break;
            }
        }
    }

    cat.Descarrega();
    UnloadRenderTexture(fboThumb);
    scene.Unload();
    UnloadShader(vs.sh);
    CloseWindow();
    return 0;
}
