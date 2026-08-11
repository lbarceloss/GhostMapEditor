# Portal (booster) e soundbox

## Portal

O botão **PORTAL** no catálogo planta o objeto já com a receita do Wiz City:

* `script = "60 70"` — são **dois floats** lidos por `sscanf("%f %f")` na física.
  A conta é `A = A/3` (se `A < 0.1` vira 1), `B = A + B`, e aí

  ```
  velocidade = (A + |vY|) * eixoY + (B + |vZ|) * eixoZ
  ```

  Ou seja, `"60 70"` = sobe `20 + |vY|` e avança `90 + |vZ|`.
* `anim = 4` (o anel gira sozinho), `coll = 0` (a bola atravessa em vez de bater);
* sobe **90 unidades** — é onde fica o centro do aro nos 15 portais do Wiz City.

**A direção do empurrão é só a rotação do elemento**: o eixo Z é pra onde a bola
sai. A seta azul do objeto selecionado mostra esse eixo. Onde a bola acerta o aro
não importa.

> ⚠️ **O elemento sozinho não basta.** Quem liga o portal de verdade é a classe
> `부스터(통과)` do `<mapa>_property.xml`: é a lista de texturas dela que diz ao
> motor que aquela superfície empurra. No Spring Wind a classe existe mas está
> **vazia**. Isso está fora do editor — é edição do `property.xml`.

## Soundbox (spawn de NPC)

`N` põe uma caixa. `[` e `]` mudam o tamanho dela. O campo de script é literal,
do jeito que a Ntreev escreve:

```
*type 0 *pet NPC_SeaGull.pet *num 5
```

`*num` é quantos bichos nascem dentro da caixa.
