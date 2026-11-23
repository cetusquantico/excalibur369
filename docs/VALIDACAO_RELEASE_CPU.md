# Validação da versão CPU 0.1.0 — continuação da auditoria

**Atualização após teste do usuário:** foi identificada uma limitação de
planejamento em intervalos grandes não alinhados, com milhões de descartes e
zero candidatos. Os resultados abaixo continuam sendo evidência dos casos
executados, mas não atestam prontidão geral da versão.
Veja o [incidente do modo reverso](INCIDENTE_REVERSE_INTERVALO.md).

As melhorias desta etapa foram implementadas e verificadas localmente. A licença
[MIT](../LICENSE) está ativa na raiz. O executável de uso habitual, ./kh71, aponta
para a compilação atualizada e portátil, com EX369_NATIVE=OFF.

## Mudanças entregues

- Saída opcional de resultados confirmados em JSONL, com permissões 0600,
  sincronização por registro e identificação de verificação ou acerto confirmado.
  O registro é gravado antes da saída para um terminal ou pipe que pode fechar.
  Falhas de gravação retornam erro; arquivos truncados não são reutilizados
  silenciosamente.
- Travas de escrita para os arquivos de checkpoint e resultados. A trava do
  checkpoint é adquirida antes de carregar a retomada. Colisões entre caminhos
  de saída, checkpoint, travas e arquivos de entrada são recusadas.
- Comandos de diagnóstico --self-test e --build-info. A saída diferencia
  instruções disponíveis, implementações compiladas e implementação selecionada.
- Detecção de AVX condicionada ao suporte da CPU e do sistema operacional;
  testes para estado vetorial incompleto e configurações de compilação diferentes.
- Compilação opcional somente escalar e isolamento dos objetos SIMD contra LTO.
- Diagnóstico reproduzível de hashes em buffers sintéticos, com amostras brutas,
  aquecimento, mediana, dispersão, compilador, opções e checksum do executável.
- Correção das unidades do microbench para nanossegundos, eliminando a conversão
  baseada em uma frequência presumida de 3,5 GHz.
- Documentação do comportamento do reverso, das probabilidades e dos limites
  de duas instâncias independentes. Estimativas de ganhos ainda não medidos
  foram removidas da lista de melhorias futuras.

Os arquivos pessoais p31_val.json, p71.json, puzzle31.txt e puzzle71.txt foram
conferidos por SHA-256 e preservados. Os arquivos dos diretórios src/ec e src/hash
permanecem idênticos ao início desta etapa. As alterações do motor nesta etapa
tratam da saída de resultados e da descrição do plano, sem mudar sua aritmética
ou implementar um novo algoritmo de busca.

## Verificações executadas

| Compilação local | Resultado |
|---|---|
| GCC, 5x52, portátil | 15/15 suítes |
| GCC, 4x64, portátil | 15/15 suítes |
| Clang, 5x52, AddressSanitizer + UndefinedBehaviorSanitizer | 15/15 suítes |
| GCC, 5x52, somente escalar | 15/15 suítes |
| GCC, 5x52, LTO com objetos SIMD isolados | 15/15 suítes |
| Executável instalado ./kh71, GCC 5x52 portátil | 15/15 suítes |

Em cada configuração houve 115 casos do puzzle 39 já resolvido. Foram
acrescentados testes do padrão reverso da CLI, direção explícita, extremos,
lotes, threads e fronteiras. Os novos testes de CPU têm 6.338 asserções; o teste
do enumerador reverso tem 27. Os testes da CLI têm 39 verificações por processo
e os de arquivos incluem 23 chamadas verificadas, além de processos de sinais
e do teste de saída por pipe fechado.

SIGINT, SIGTERM e SIGKILL interromperam execuções reais em um intervalo sintético
limitado; as retomadas terminaram com todos os tickets concluídos. Segmentos
incompletos podem ser repetidos. Isso não é uma simulação de perda de energia.

Na emulação QEMU foram verificados seis modelos: qemu64, Nehalem,
Haswell-noTSX, EPYC-v4, EPYC-Genoa-v1 e SapphireRapids-v1. Todos passaram no
self-test, no teste de hashing, na seleção de CPU e no enumerador reverso,
tanto com a compilação portátil comum quanto com LTO.

Os 115 casos do puzzle 39 também passaram pelo motor real em cada um dos seis
modelos emulados: 690 casos nessa matriz. O caminho AVX2 foi efetivamente
selecionado e executado nos modelos compatíveis. A comparação independente usa
coincurve/libsecp256k1 e hashlib. Nenhuma faixa completa de puzzle não resolvido
foi pesquisada para obter esses resultados.

## Reverso e duas máquinas

A cobertura dos casos testados está correta. O motor com lotes reverte suas
faixas internas; isso não corresponde a uma única sequência global decrescente.
O plano agora informa essa limitação quando aplicável.

Duas máquinas semelhantes, com trabalho distinto e coordenado, podem aproximar
o tempo de execução da metade. O trabalho total da cobertura completa permanece
o mesmo. Cada metade tem 50% de probabilidade de conter uma solução apenas sob
a hipótese de distribuição uniforme e divisão em partes iguais. Duas instâncias
independentes não implementam essa coordenação.

O texto completo e um exemplo de 100 posições estão em
[REVERSE_MODE.md](REVERSE_MODE.md). Não foi estabelecido ineditismo do recurso.

## Compatibilidade e publicação

Os testes físicos continuam limitados ao i5-3470. Emulação não mede desempenho,
topologia híbrida, NUMA ou estabilidade de uma máquina moderna real. Outros
sistemas operacionais e arquiteturas permanecem sem validação de release.
[CPU_COMPATIBILITY.md](CPU_COMPATIBILITY.md) descreve os pontos de extensão,
seleção de instruções e limites de portabilidade entre distribuições Linux.

O workflow foi ampliado para 12 configurações: quatro combinações de
compilador/corpo, uma com sanitizadores, uma somente escalar e seis CPUs
emuladas. O YAML foi validado localmente. A execução no GitHub permanece
pendente de publicação; este checkout não tem um remoto configurado.

Esta etapa não estabelece um novo ganho de candidatos por segundo. O diagnóstico
de hashes mede somente uma parte do processamento. O ganho aparente de cerca
de 2% registrado na auditoria anterior continua inconclusivo, e foi medido com
outra configuração de compilação.

## Evidências locais

Snapshot anterior, logs, diagnóstico JSON, cópia do executável anterior e patch
das alterações estão em:

    /root/excalibur369-release-audit-qwf85bwb/

Os logs finais usam os prefixos final-tests, final-qemu e final-puzzle39.
O executável anterior à recompilação fica em before-installed-kh71.
A [auditoria inicial](AUDITORIA_CPU_2026-09-12.md) registra a primeira rodada
de correções e sua comparação de desempenho.
