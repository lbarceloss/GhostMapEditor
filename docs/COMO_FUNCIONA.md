# Como isto é construído

## O editor não escreve `.gbin`

Ele grava um `.json` com a **lista de operações** (`add` / `mover` / `apagar` /
`soundbox`) e o `bin\tools\aplicar.py` faz a escrita, usando o round-trip do
[pet-source_tools](https://github.com/Acrisio-Filho/pet-source_tools), que já foi
provado semanticamente lossless nos 18 holes do Spring Wind (só zera lixo de
memória que a Ntreev deixou depois do `\0` nos campos de string fixa).

Ter um segundo escritor de `.gbin` em C++ criaria dois caminhos pra dar
manutenção — por isso a escrita mora num lugar só.

O caminho completo:

```
GhostMapEditor.exe
    grava  projetos\<hole>_edicao.json
    chama  python tools\aplicar.py <projeto>.json --saida saida\
                                                        ↓
                                              saida\<hole>.gbin
```

O log da última geração fica em `bin\ultimo_gerar.log` — é o primeiro lugar pra
olhar quando o botão `GERAR .gbin` reclamar.

## A lista de mapas e holes

Os mapas do PangYa são estáticos — um punhado de rounds, 18 holes cada — então
não faz sentido caçar arquivo numa caixa de diálogo. A `Biblioteca` varre as
raízes de asset uma vez procurando `.gbin` e agrupa por pasta de round:

* o `.gbin` mora em `<round>\map\`, `<round>\rank\` ou solto na pasta do round —
  quem resolve isso é `PastaDoMapa()`;
* o nome que aparece na tela sai de `NomeBonito()`: `round10_spring wind` vira
  `Spring Wind`;
* o número do hole é o número no fim do nome do arquivo. **Quem não tem número
  (o `<mapa>_rank.gbin`) aparece como `EXTRA`, no fim da grade** — senão o rank
  ficaria na frente do hole 1;
* na hora de juntar, o critério de desempate é o **mesmo do `AssetDB`**: caminho
  mais curto ganha. Sem isso, duas raízes que se sobrepõem duplicariam o hole.

O índice é montado uma vez e fica em memória; o botão `reindexar` refaz, e
arrastar uma pasta pra janela invalida o cache sozinho.

🩸 **Trocar de hole só acontece depois do `EndDrawing()`.** O clique na grade só
guarda o caminho em `abrirDepois`; quem chama o `abrir()` é o fim do quadro. É
que o `abrir()` descarrega texturas e modelos — fazer isso no meio do desenho é
pedir crash.

### Pacote de mapas

Uma pasta chamada `mapas` (ou `data`, ou `texture_dds`) **ao lado do exe** entra
sozinha como raiz de asset, antes de qualquer coisa do `editor.cfg`. É o que faz
o programa funcionar recém-baixado: descompactou o pacote ali, abriu, escolheu.
Quem monta esse pacote a partir da instalação do próprio usuário é o
`tools/montar_pacote.py`.

O `editor.cfg` também é procurado ao lado do exe quando não está no diretório
atual — é o caso de quem clica no exe direto pelo Explorer.

## Preservação da matriz original

Elementos da Ntreev têm inclinação e escala não uniforme. O editor **nunca
decompõe** a matriz em "rotação Y + escala": decompor corromperia esses
elementos. Ele guarda a 3×3 original em `baseM` e aplica um delta, na convenção
**vetor-linha**:

```
M = baseM · (escala · Ry(giro))
```

Um elemento original que você só arrastou mantém exatamente a inclinação que
tinha. Elemento novo nasce com `baseM` = identidade, então pra ele a conta bate
com a do plantio direto.

## AABB

O `.gbin` guarda duas caixas por elemento, e o `aplicar.py` recalcula as duas em
mundo. A fórmula foi validada contra **3.269 elementos originais** dos 18 holes,
com erro 0,000:

* `min_max` — AABB exata dos vértices transformados;
* `fit_base_model` — XZ só dos vértices com `y <= miny + 1.0`, e Y indo de `miny`
  até `miny + 10` (o 10 é literal, **não** escala com o modelo).

O `aplicar.py` também declara no `.gbin` as texturas do modelo novo — é o que o
`wizcity_02` (que tem portal) faz e o `wizcity_01` (que não tem) não faz. Vale
pra qualquer modelo novo, não só pro portal.

## O índice de arquivos bate nos dois lados

O `Indice` do Python usa o **mesmo critério** do `AssetDB` do C++: no empate de
nome, fica com o caminho **mais curto**. Se divergisse, a AABB calculada no
Python não bateria com o modelo que você viu na tela.

## Código compartilhado com o Ghost Pangya SIM

`src/shared/` é o núcleo reutilizável: `viewer_core` (shader, montagem de malha,
cena, câmera livre, céu, UI), os leitores de `.gbin` / `.pet` / `.dds`, o raycast
do terreno e a camada Win32 (drag-and-drop, diálogo de arquivo, ícone).

No repositório de origem esses arquivos são compilados direto da pasta do SIM —
não existem duas cópias lá. Aqui eles vêm junto pra que este repositório compile
sozinho. **Mexeu no `viewer_core`, os dois aplicativos mudam.**

O teste que vale a pena repetir depois de mexer nele é o **pixel-diff**: renderize
o mesmo hole antes e depois com `--shot` e compare. No refactor que criou o
`viewer_core` a diferença foi de 392 px, todos no contador de FPS.

## Raycast do mouse no terreno

Marcha ao longo do raio procurando a troca de sinal entre a altura do raio e a do
terreno (`GroundGrid::HeightAt`), depois faz bisseção de 48 passos. O passo cresce
com a distância (`t += 6 + t*0.02`), senão fica caro em hole grande. O ponto vira
o "cursor no chão" que aparece no HUD em coordenadas PangYa.

## Miniaturas do catálogo

As miniaturas **não podem** ser geradas dentro de um `BeginScissorMode`: o
scissor ativo da lista recorta também o framebuffer da miniatura e elas saem
vazias. Por isso a lista só **enfileira**, e a geração acontece no começo do
quadro seguinte, antes do `BeginDrawing`, três por quadro.

## Modo `--shot` (o auto-teste)

Renderiza uns quadros e sai — foi assim que o editor foi validado, sem clicar:

```bash
bin\GhostMapEditor.exe hole.gbin --shot tela.png --cam x,y,z --look x,y,z --buscar bigtree --plantar 0
```

| flag | |
|---|---|
| `--shot <png>` | renderiza 40 quadros, salva a tela e sai |
| `--cam x,y,z` / `--look x,y,z` | posiciona a câmera |
| `--buscar <texto>` | filtra o catálogo |
| `--plantar <n>` | entra no modo plantar com o n-ésimo modelo da lista filtrada |
| `--auto <projeto.json>` | planta 3, apaga um original, move outro, grava o projeto **e dispara o botão GERAR** |
| `--mapas` | abre a lista de mapas e holes (com `--buscar`, já filtrada) |
| `--escolher M,H` | escolhe o mapa M e abre o hole H — `H = -1` só seleciona o mapa |

`--auto` exige `--plantar`, porque ele precisa estar no modo plantar com o ghost
sobre o terreno. E ele chama **a mesma função** que o clique chama — se fosse
código duplicado, o teste não provaria nada.
