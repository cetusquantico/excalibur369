# Reverse: comportamento e limites

**Pendência conhecida:** o início da execução pode ficar ocupado com uma
quantidade enorme de segmentos fora de um intervalo não alinhado. A contagem
de segmentos concluídos não comprova candidatos verificados.
Veja o [incidente e os diagnósticos](INCIDENTE_REVERSE_INTERVALO.md).

O modo reverso muda a ordem de visita. Ele não muda o intervalo configurado,
o conjunto de resultados possíveis nem a probabilidade de uma posição conter
a solução.

## O comportamento implementado

- A ordem global reversa entrega primeiro os segmentos de índice maior.
- Quando a direção não foi especificada, a CLI também seleciona a direção
  reversa dentro das faixas internas de processamento.
- Uma direção explícita tem prioridade sobre esse padrão.
- O motor divide um segmento em várias faixas internas para processar lotes.
  Cada faixa desce independentemente. O restante que não cabe no lote é
  processado separadamente. Portanto, mesmo uma única thread com lotes não
  produz necessariamente uma sequência global estritamente decrescente.
- Várias threads podem terminar segmentos fora da ordem em que os receberam.
- Rotação de bytes fixos e round-robin têm suas próprias ordens. Reverter os
  segmentos não inverte automaticamente a rotação de padrões.

O enumerador de referência usado nos testes percorre valores individualmente.
Seu teste de sequência decrescente não deve ser apresentado como uma gravação
da ordem temporal do motor com lotes. Os testes da CLI verificam separadamente
a execução real, usando apenas janelas pequenas do puzzle 39 já resolvido.

## Duas máquinas e os 50%

Suponha uma única solução, uniformemente distribuída em 100 posições.
Se cada máquina receber 50 posições distintas, cada metade tem 50% de
probabilidade de conter a solução. São 50 posições por máquina e 100 posições
no conjunto. Para intervalos com tamanho ímpar, as duas partes diferem por uma
posição.

Se duas máquinas de mesma velocidade cobrirem partes diferentes simultaneamente,
o tempo para cobrir tudo pode se aproximar da metade. Isso exige ausência de
duplicações relevantes, velocidades semelhantes e tratamento das fronteiras.
O trabalho total para cobrir todo o intervalo continua sendo o mesmo.

Começar por pontas opostas pode produzir esse benefício quando as fronteiras
avançam sem sobreposição e existe uma regra para parar no encontro. Duas
instâncias independentes com o intervalo completo não possuem essa coordenação.
Velocidades diferentes, lotes, padrões fixos e reinícios também afetam a
distribuição temporal do trabalho.

Exemplo de probabilidade por cobertura, supondo posições distintas:

| Posições concluídas pela primeira | Pela segunda | Cobertura conjunta | Probabilidade de ter incluído a solução |
|---:|---:|---:|---:|
| 10 | 10 | 20 de 100 | 20% |
| 25 | 25 | 50 de 100 | 50% |
| 50 | 50 | 100 de 100 | 100% |

Se ambas verificarem as mesmas 10 posições, a cobertura conjunta é 10,
e não 20. Sem uma distribuição uniforme conhecida, a fração coberta não basta
para atribuir esses percentuais à localização da solução.

O tamanho absoluto do intervalo continua determinando o tempo necessário.
A relação aproximada de duas máquinas semelhantes não torna uma faixa enorme
pequena. Também não há evidência nesta auditoria para anunciar exclusividade
ou ineditismo do modo reverso.

## Testes e retomada

Os testes contemplam os extremos inclusivos, intervalo unitário, limites entre
segmentos, faixas não alinhadas, direção explícita, lotes, threads, padrões
fixos e retomada. O arquivo de checkpoint registra ordem e direção: uma
mudança incompatível é recusada.

Uma interrupção pode exigir repetir o segmento incompleto. A contagem acumulada
de candidatos inclui essa repetição; não é uma contagem exata de posições
distintas já cobertas. O arquivo JSONL pode igualmente conter um resultado
repetido após uma retomada.

Esta versão não implementa coordenação entre máquinas. Não anuncia uma garantia
de 50% de redução das buscas nem uma ordem global estritamente decrescente em
execução com lotes.
