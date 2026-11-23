# EXCALIBUR369

Caçador de chaves **CPU-first** para as faixas dos **puzzles públicos de Bitcoin**.

Construído em torno de uma única métrica: **candidatos válidos completos por
segundo**, onde um candidato só é contado depois de percorrer todo o caminho:

```
escalar privado → chave pública comprimida secp256k1 → SHA-256 → RIPEMD-160 → comparação com o alvo
```

Sem GPU nesta fase. `Base58Check` **nunca** roda dentro do laço quente.

> **Escopo.** Esta ferramenta é para os endereços dos puzzles públicos de
> Bitcoin, cujas faixas são divulgadas de propósito como desafio criptográfico.
> Ela não oferece vantagem contra chaves comuns. Mesmo a faixa publicada
> de 2⁷⁰ do puzzle 71 exige milhões de anos para uma varredura completa em
> uma CPU nas taxas documentadas. Rotacionar bytes fixos muda a ordem;
> percorrer todos os padrões mantém o tamanho total do espaço.

*(English version: [README.md](README.md))*

---

## Compilação

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
(cd build && ctest --output-on-failure)
```

Execute `./build/kh71`. O atalho na raiz é opcional:
configure com `-DEX369_ROOT_LINK=ON` para criar `./kh71`.

Validado em Linux x86-64, GCC e Clang com C++17 e `unsigned __int128`,
CMake 3.16+ e threads POSIX. Não exige bibliotecas externas em execução.
Outros sistemas e arquiteturas ainda não foram validados nesta versão.
(`tests/reference_check.py` também confere contra `coincurve`/libsecp256k1
quando o módulo está instalado; caso contrário ele se auto-ignora.)

### Escolha da representação do corpo finito

```bash
cmake -S . -B build -DEX369_FIELD=5x52   # padrão (insaturado, mais rápido)
cmake -S . -B build -DEX369_FIELD=4x64   # referência saturada
```

As duas implementam a mesma API e **a suíte de testes inteira roda contra a que
for selecionada** — nenhuma das duas é um caminho só de benchmark.

## Começando

```bash
./build/kh71 --cpu-info                                  # o que esta CPU suporta
./build/kh71 --puzzle 71 --address <END> --dry-run       # mostra o plano, não busca nada
./build/kh71 --puzzle 71 --address <END> --mode sequential
```

## A linha de comando

```
kh71 --mode sequential
kh71 --mode reverse
kh71 --mode random --seed 12345

kh71 --fixed on --fixed-count 3 --fixed-positions auto \
     --fixed-rotation random-no-repeat

kh71 --fixed on --fixed-values AA,BB,CC --fixed-positions 1,2,3 \
     --round-robin on --slice-keys 1073741824
```

`--help` lista todas as opções. As principais:

| opção | significado |
|---|---|
| `--puzzle N` / `--range LO:HI` | o que buscar |
| `--address` / `--hash160` | um alvo único |
| `--file CAMINHO`, `-f` | arquivo de alvos: um endereço P2PKH ou HASH160 de 40 hex por linha |
| `--status S` | imprime uma linha de progresso (taxa, total, %, ETA) a cada S segundos |
| `--mode` | ordenação **global** dos segmentos: sequential, reverse, random-no-repeat |
| `--direction` | caminhada **local** dentro do segmento: `P += G` ou `P -= G` |
| `--fixed*` | o motor de bytes fixos (totalmente chaveável, `--fixed off` por padrão) |
| `--round-robin` / `--slice-keys` | intercalação opcional de padrões |
| `--checkpoint` / `--resume` | progresso durável |
| `--output CAMINHO` | resultados confirmados em JSONL com permissões privadas |
| `--self-test` / `--build-info` | verificar a instalação / mostrar dados da compilação |
| `--simd` | força `scalar`/`sse`/`avx2`; o padrão é o melhor que a CPU suporta |
| `--verify CHAVEHEX` | roda o verificador independente numa chave e sai |

As posições de bytes são numeradas a partir do primeiro byte **ativo** da faixa
(então `1,2,3` significa "o 2º, 3º e 4º bytes que a faixa deixa variar");
`--fixed-index-base absolute` muda para índices brutos 0..31.

---

## O que a arquitetura garante

**Cobertura completa sem repetir trabalho numa execução ininterrupta.** Todo trabalho é endereçado por um
ticket monotônico que passa por *bijeções* até (padrão, segmento).
Random-no-repeat é uma permutação de Feistel com chave, então "não repete" é uma
propriedade **estrutural** e custa O(1) de memória — não um conjunto de
visitados de tamanho 2⁷⁰. Pegar trabalho é um único `fetch_add` atômico: não há
mutex no caminho quente.

**Um byte fixo que cruza o prefixo da faixa é tratado corretamente.** No puzzle
71 o byte ativo mais alto tem só 6 bits livres, ou seja 64 valores legais e não
256. Errar isso geraria chaves fora da faixa *e* varreria o mesmo espaço quatro
vezes. `--fixed-count 3` no puzzle 71 dá exatamente 2²² padrões × 2⁴⁸ chaves =
2⁷⁰ — o espaço inteiro, uma única vez.

**Retomar nunca pula.** Um segmento interrompido no meio é deliberadamente
deixado incompleto, então a marca-d'água trava nele e o resume o refaz. Refazer
um pouco de trabalho é seguro; pular silenciosamente não é.

**Um acerto nunca é confiado ao laço quente.** `verify_candidate()` recalcula o
candidato por um caminho de código propositalmente diferente — double-and-add
afim em vez do motor Jacobiano em lote, os hashes genéricos de streaming em vez
das especializações de bloco único — e reconstrói o endereço. `FOUND` só é
impresso se as duas versões concordarem.

Veja [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) para o projeto completo e
[docs/USAGE.md](docs/USAGE.md) para as notas de uso.

---

## Testes

```
test_hash        vetores conhecidos de hash/endereço, bloco-único == genérico
test_hash_simd   cada backend SIMD contra o escalar, bit a bit
test_ec          leis de corpo, chave→pubkey→hash160→endereço,
                 travessia direta (k,k+1,k+2) e reversa (k,k-1,k-2)
test_coverage    cada modo/rotação/round-robin visita seu espaço EXATAMENTE
                 uma vez — sem duplicatas, sem omissões
test_engine      o laço quente real acha chaves plantadas em toda configuração
test_checkpoint  o resume não perde nada; segmento interrompido é refeito
reference_check  conferência cruzada contra libsecp256k1 (implementação independente)
test_regressions regressões de faixas, entradas e configurações
test_cpu        seleção conforme CPU, sistema operacional e compilação
test_reverse    ordem do enumerador de referência em intervalos sintéticos pequenos
test_cli        comportamento e erros da linha de comando
test_run_files  persistência, travas, sinais e retomada em reverso
public_puzzle39 vetor público resolvido, incluindo o motor real em reverso
self_test       diagnóstico da instalação com vetores conhecidos
hash_diagnostics hashes sintéticos e conferência com a referência
```

A suíte original tinha 27.515 asserções. A versão 0.1.0 acrescenta regressões
para faixas, checkpoints, entradas inválidas e o vetor público do puzzle 39.
Os resultados atuais estão na [auditoria](docs/AUDITORIA_CPU_2026-09-12.md).

---

## Desempenho

Medido, não afirmado. O registro completo está em
[docs/BENCHMARKS.md](docs/BENCHMARKS.md) — **incluindo as seis otimizações que
foram testadas, medidas e rejeitadas por serem mais lentas.**

Máquina: Intel i5-3470 (Ivy Bridge), 4 núcleos físicos, sem SMT, **sem AVX2**.

| etapa | M cand/s | vs anterior | vs V0 |
|---|---|---|---|
| V0 linha de base | 2.550 | — | — |
| + HASH160 multi-buffer SSE 4 vias | 5.896 | +131,2% | +131,2% |
| + batch 512 | 6.125 | +3,9% | +140,2% |
| + afinidade de CPU | 6.481 | +5,8% | +154,2% |
| **+ corpo 5×52 insaturado** | **7.095** | **+7,4%** | **+178,2%** |

Metodologia: `bench/run_bench.sh` roda a mesma carga N vezes e reporta a
**mediana** com a faixa observada, porque execuções isoladas nesta máquina
variam ~±3%. Variantes próximas são compiladas como binários separados e
executadas **intercaladas**, para que a deriva térmica não fabrique um resultado.

```bash
BENCH_REPS=9 ./bench/run_bench.sh "minha mudança"
./build/microbench          # custo por etapa
```

### Onde o tempo vai (por candidato, 1 núcleo, B=512)

```
etapa EC                  777,6 cyc
+ serializar pubkey        ~60 cyc
+ hash160 (SSE ×4)        ~600 cyc
+ comparar com o alvo       ~10 cyc
```

---

## Equivalência com o keyhunt

| keyhunt | EXCALIBUR369 |
|---|---|
| `-m address` | *(implícito — é o único modo)* |
| `-f alvos.txt` | `-f alvos.txt` |
| `-r LO:HI` | `--range LO:HI` |
| `-t $(nproc)` | `--threads $(nproc)` |
| `-s 9` | `--status 9` |
| `-l compress` | *(implícito — o pipeline é só comprimido)* |
| `-R` | `--mode random` (ver abaixo) |
| `-q` | `--quiet` |
| — | `--affinity on`, `--checkpoint arq.json` |

```bash
./build/kh71 -f puzzle71.txt \
       --range 4ade9d000000000000:4ade9dffffffffffff \
       --threads $(nproc) --status 9 --mode random \
       --affinity on --checkpoint p71.json --quiet
```

O `-R` do keyhunt sorteia pontos de partida e **pode repetir** chaves já
testadas. Aqui `--mode random` é uma permutação de Feistel sobre os segmentos:
cobre o espaço inteiro sem repetir nem pular, com O(1) de memória. Use
`--seed N` para reproduzir a mesma ordem depois — sem `--seed` a ordem é fixa
(seed 0), não aleatória por execução.

Uma linha inválida no arquivo de alvos é **erro**, não é ignorada em silêncio:
um typo significaria dias buscando a coisa errada.

## Estado

Pendência conhecida: intervalos grandes não alinhados podem concluir milhões de
segmentos no agendador sem verificar candidatos. Há contadores separados para
explicar os descartes; a limitação de planejamento permanece aberta. Veja
[o incidente do modo reverso](docs/INCIDENTE_REVERSE_INTERVALO.md).

Versão CPU 0.1.0. Veja [CHANGELOG.md](CHANGELOG.md) e a
[auditoria](docs/AUDITORIA_CPU_2026-09-12.md) para correções e limitações.
Os resultados desta continuação estão na [validação da versão CPU](docs/VALIDACAO_RELEASE_CPU.md).
GPU fica fora desta versão.

O reverso ordena os segmentos e inverte as caminhadas das faixas internas.
Com lotes e várias threads, isso não corresponde a uma única sequência global
decrescente. Duas instâncias independentes não coordenam a cobertura. Veja
[o comportamento do reverso e os 50%](docs/REVERSE_MODE.md).

Para validar uma versão, instale `tests/requirements.txt` em um ambiente
virtual Python e configure com `-DEX369_REQUIRE_REFERENCE=ON`. A integração
contínua exige a referência independente e testa os dois corpos com GCC/Clang.
O puzzle 39 resolvido é validado em janelas pequenas ao redor da chave conhecida.

A compilação é portátil por padrão. `-DEX369_NATIVE=ON` permite otimizar para
a CPU local; esse binário pode não funcionar em outro processador.
Veja [CONTRIBUTING.md](CONTRIBUTING.md) para contribuir.

A [documentação de compatibilidade](docs/CPU_COMPATIBILITY.md) explica CPUs novas,
emulação, seleção de instruções, diagnósticos e pontos de extensão. As taxas
históricas acima dependem da carga e do hardware utilizados.

## Créditos

Construído sobre ideias e ferramentas do [keyhunt](https://github.com/albertobsd/keyhunt),
de Luis Alberto (albertobsd), usado sob a licença MIT — Copyright (c) 2020 Luis Alberto.

## Apoie

Trabalho de tempo livre, bancado por mim. A versão GPU é o próximo passo grande e precisa de
hardware. Se isto te for útil e quiser ajudar, doações em Bitcoin são bem-vindas, sem
promessa de retorno:

`bc1qhsq406jt6ghkzmhpnqe4r2s639t6mtrhj6zx03`

## Licença

O projeto é open source sob a [licença MIT](LICENSE).
