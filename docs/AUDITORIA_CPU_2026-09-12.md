# Auditoria e correções da versão CPU — 12/09/2026

O código inicial passou na suíte existente, mas tinha falhas capazes de encerrar
uma busca sem examinar candidatos válidos. As correções abaixo foram aplicadas
com autorização do autor. A versão local é **0.1.0**, candidata à primeira
publicação. Os resultados descrevem os cenários efetivamente testados; não são
uma prova formal de ausência de falhas.

## Resultado das verificações

Ambiente: Linux x86-64, Intel i5-3470, quatro núcleos, SSE/SSSE3, sem AVX2;
GCC 15.3.0, Clang 21.1.8, CMake 4.3.4, Python 3.13.14, coincurve 21.0.0.

| Compilação corrigida | Resultado |
|---|---|
| GCC, Release, corpo 5x52, portátil | 10/10 testes aprovados |
| GCC, Release, corpo 4x64, portátil | 10/10 testes aprovados |
| Clang, 5x52, AddressSanitizer + UndefinedBehaviorSanitizer | 10/10 testes aprovados, sem diagnóstico dos sanitizadores |
| GCC, Release, 5x52, `EX369_NATIVE=ON` | 10/10 testes aprovados |

Cada execução inclui 27.679 asserções C++/referência, 37 verificações de processo
da CLI e 74 cenários do puzzle 39. Os 10 testes são grupos, não apenas dez casos.
Antes das correções, as sete suítes originais também passavam: 27.515 asserções.
Isso mostrou a necessidade de incluir regressões no caminho real de execução.

Na auditoria inicial, os núcleos criptográficos também foram comparados com
implementações independentes: 21.640 verificações de operações de corpo contra
inteiros Python, 1.285 chaves públicas contra libsecp256k1 e 1.112 verificações
de SHA-256/RIPEMD-160 contra hashlib, em **cada** representação. Não houve
divergência. Os arquivos de aritmética da curva e dos hashes permaneceram iguais
durante as correções. O gerador de RIPEMD reproduziu exatamente o arquivo incluído.

## Puzzle 39 fornecido pelo autor

| Campo | Valor |
|---|---|
| Início | `4000000000` |
| Fim | `7fffffffff` |
| Solução pública | `4b5f8303e9` |
| Endereço | `122AJhKLEfkFBaGAd84pLp1kfE7xK3GdT8` |
| HASH160 conferido | `0b304f2a79a027270276533fe1ed4eff30910876` |

A referência independente confirmou a solução e o endereço. O teste
[public_puzzle39](../tests/test_public_puzzle39.py) cobre modos sequencial,
reverso e aleatório, direções, uma e quatro threads, scalar/SSE, rotação de
bytes fixos, round-robin, diferentes lotes, máscaras com passo maior que um e
com lacunas, bordas e exclusão de chaves fora da janela.

São janelas pequenas em torno da solução conhecida. A faixa inteira tem
274.877.906.944 candidatos e **não foi varrida integralmente** nesta auditoria.

## Achados e correções

Prioridade alta indica possibilidade de omissão silenciosa ou configuração
incorreta da busca. Prioridade média indica robustez, portabilidade ou observabilidade.

| Prioridade | Problema confirmado no código inicial | Correção e evidência |
|---|---|---|
| Alta | Uma faixa não alinhada, inteiramente dentro de um segmento, era descartada porque os dois extremos do segmento estavam fora dela. `1234:1a5c` terminou com zero candidatos, embora tivesse 2.089, em todos os modos. | O recorte compara separadamente os limites inferior e superior. A regressão verifica a contagem do motor real, inclusive faixas de uma chave. |
| Alta | O fingerprint omitia os valores manuais dos bytes fixos. Após terminar o subconjunto `1A`, a retomada com `1B` aceitava o checkpoint e examinava zero candidatos. | O fingerprint identifica modo manual/automático, valores e ordem das posições. Alterações incompatíveis são recusadas. |
| Alta | Ordenar as posições sem ordenar seus valores trocava a associação. Posições `2,1` com valores `11,22` produziam base `10112200`, quando a associação solicitada era `10221100`. | A resolução preserva a ordem fornecida. Teste verifica a associação exata. |
| Alta | Checkpoints truncados, versões futuras e progresso fora dos limites eram aceitos. | Parser estrito, tipos obrigatórios, rejeição de campos duplicados, validação de tickets e checksum do estado no formato 2. Casos inválidos são recusados antes de executar. |
| Alta | Com 127 bits livres, `total_indices()` retornava `2^128−1`; a soma para arredondamento transbordava e o plano apresentava zero segmentos por padrão. | Representação exata de `2^127` e divisão sem soma sujeita a overflow. Contagens de tickets não representáveis são rejeitadas. |
| Média | `--resume` sem arquivo, com arquivo ausente ou ilegível reiniciava silenciosamente. | A retomada exige caminho e carga válida; o erro é visível e retorna código não zero. |
| Média | `atoi`/conversões permissivas aceitavam lixo, negativos e overflow; o verificador retornava sucesso para escalar zero ou maior/igual à ordem da curva. | Conversões verificadas, limites explícitos, validação da faixa de escalares e códigos de erro adequados. Uma faixa de chave única agora funciona. |
| Média | Retomar zerava o total persistido e o tempo; o tamanho exibido para faixas recortadas podia ser maior que o real. | Totais anteriores são preservados; duas etapas de 64 candidatos persistem 128. O denominador considera o intervalo válido. |
| Média | A gravação usava temporário previsível, não sincronizava disco e ignorava erros finais. | Temporário exclusivo no mesmo diretório, sincronização de arquivo e diretório, rename e propagação de falhas. Resultado confirmado é impresso imediatamente. |
| Média | `-march=native` era padrão em todos os arquivos, apesar da promessa de binário portátil. | Compilação portátil por padrão, opção nativa explícita e documentação coerente. Opções CMake inválidas são rejeitadas. |
| Média | Topologia/afinidade ignoravam os CPUs permitidos ao processo. | Detecção respeita `sched_getaffinity`; teste local restrito a um CPU reportou um núcleo e uma thread. Falha de afinidade agora avisa. |
| Média | A ausência de coincurve aparecia como teste aprovado; faltavam testes da CLI e automação de contribuição. | Skip explícito de código 77, referência obrigatória para validação de release, regressões e workflow de CI. |

O formato 2 detecta corrupção acidental, incluindo alteração de um progresso
ainda numericamente válido. O checksum não autentica um arquivo recebido de
terceiros. Use um processo por caminho de checkpoint.

O uso de `fsync` no arquivo e no diretório segue a distinção entre conteúdo
persistido e entrada de diretório persistida descrita no
[manual Linux](https://man7.org/linux/man-pages/man2/fsync.2.html).
O alcance de `-march=native` foi conferido na
[documentação do GCC](https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html).

## Compatibilidade com os arquivos existentes

`p31_val.json` e `p71.json` foram aceitos com `--resume --dry-run` pelo binário
corrigido. Seus conteúdos foram conferidos por SHA-256 antes e depois e não
mudaram. Os dois usam a versão 1 sem bytes fixos.

A compatibilidade com a versão 1 fica limitada a faixas alinhadas sem bytes
fixos. As outras configurações antigas não podem ter sua cobertura garantida,
pois o código anterior podia omitir trabalho ou usar outra associação de bytes.
Arquivos novos usam a versão 2. Não foi feita migração ou alteração dos arquivos
pessoais do autor.

Uma interrupção ainda pode causar repetição do segmento incompleto, conforme
o contrato conservador original. Totais acumulados contam esse trabalho repetido;
porcentagem e ETA são estimativas. O relatório final de taxa mede a invocação
atual; o checkpoint preserva o histórico acumulado disponível.

## Desempenho

A comparação controlada está registrada em [BENCHMARKS.md](BENCHMARKS.md), na
entrada de 12/09/2026. Medianas: **8.8535 → 9.0350 milhões/s**
(+2.05%). A diferença ficou dentro da variação observada;
**não há ganho de velocidade comprovado** neste experimento. Foram mantidos o mesmo compilador, opções, número de
threads, backend, lote e quantidade exata de candidatos entre os dois binários.

A correção de afinidade evita uma distribuição incorreta em ambientes com
restrição de CPUs. O scheduler deixou de alocar um nó temporário para uma
conclusão em ordem. Essas mudanças não alteram a associação entre tickets e
candidatos nem os núcleos criptográficos.

## Preparação para publicação

- Adicionados `CONTRIBUTING.md`, `SECURITY.md`, `CHANGELOG.md` e versão na CLI.
- Workflow com GCC/Clang, 5x52/4x64 e sanitizadores; ações fixadas por commit e
  permissão de leitura. Sua sintaxe YAML foi validada localmente. A execução
  hospedada ainda depende da publicação do repositório.
- Checkpoints pessoais, temporários, logs e resultados ficaram fora do conjunto
  padrão de arquivos a versionar; os arquivos existentes foram preservados.
- A busca por padrões comuns de credenciais nos arquivos de texto não encontrou
  tokens ou cabeçalhos de chaves privadas. Isso não é uma garantia absoluta de
  ausência de segredos. O repositório inicial não tinha commits ou histórico.
- Na continuação autorizada pelo autor, foi adotada a [licença MIT](../LICENSE)
  na raiz. A proposta anterior foi substituída pelo arquivo de licença ativo.

## Limites que continuam explícitos

AVX2 não pode ser executado fisicamente no i5-3470. A continuação acrescentou
validação sob emulação, documentada em [compatibilidade](CPU_COMPATIBILITY.md).
AVX-512 e GPU não fazem parte
da versão. Outros sistemas operacionais e arquiteturas não foram validados.
Não houve prova formal, simulação de queda de energia ou varredura exaustiva
dos puzzles 31, 39 ou 71. A confirmação de um acerto por um caminho diferente
compartilha primitivas com o motor; a independência de implementação vem dos
testes com libsecp256k1/hashlib.

Uma CPU pode participar dos desafios, mas rotação de bytes fixos não diminui
o número total de candidatos ao percorrer todos os padrões. A promessa anterior
de viabilidade do puzzle 71 em uma CPU foi removida dos dois READMEs.

## Evidências locais

O estado inicial, manifestos, executáveis de referência, diagnósticos e medições
ficaram em `/root/excalibur369-audit-20260912-dmv3_5en/`.
Os logs finais estão em `build-audit/`, `build-audit-4x64/`,
`build-audit-sanitize/` e `build-audit-native/`. Essas pastas de build são ignoradas
pelo Git. O relatório e os testes permanecem no projeto para revisão.
