# Incidente: zero candidatos e milhões de segmentos concluídos

Estado: causa identificada; diagnóstico de progresso corrigido. A limitação
do planejamento continua pendente. Este incidente impede tratar a versão
0.1.0 como pronta para o uso geral anunciado anteriormente.

## Sintoma observado

O comando apresentado pelo usuário continua sintaticamente válido. A execução
registrou 73.539.720 segmentos concluídos e zero candidatos, pontos EC ou hashes.
O checkpoint preservado confirma zero candidatos e a mesma marca de progresso.
Não houve evidência de busca útil nesse período.

## Causa

Para representar o intervalo, o código usa um espaço interno maior, baseado
no prefixo comum dos seus limites. Quando o intervalo não ocupa todo esse espaço,
há posições externas que precisam ser descartadas.

O agendador reverso começa pelo final desse espaço interno. Neste caso ele
entrega uma quantidade muito grande de segmentos totalmente acima do limite
solicitado. O motor os descarta corretamente, mas o agendador os marca como
concluídos. A contagem de segmentos não distingue, sozinha, esses descartes
de segmentos em que houve comparação de candidatos.

A inspeção aritmética encontrou mais de 22 trilhões de segmentos inteiramente
externos acima do limite solicitado na configuração registrada. O comando não
foi executado novamente contra o alvo: foram usados o checkpoint preservado,
o plano somente de leitura e um modelo sintético pequeno.

O caso pequeno usa o intervalo inclusivo 0x4a..0x7a dentro de um espaço interno
0x40..0x7f. Com unidades de tamanho 1, existem cinco unidades externas antes
da primeira posição válida no reverso. Ao concluir esse exemplo, são 64 unidades
do agendador, 15 descartes externos e 49 candidatos efetivamente contabilizados.
Esse modelo verifica o diagnóstico; não altera o planejamento.

## Alterações desta etapa

- O contador outside-range segs distingue unidades descartadas por estarem
  inteiramente fora do intervalo.
- O campo completed with work mostra as conclusões que não são esses descartes.
- O status e o relatório final explicam quando há descartes e nenhum candidato
  contabilizado na execução atual.
- Foram acrescentados testes limitados de contagem, descarte e apresentação.
- Os relatórios anteriores agora apontam esta limitação e não atestam prontidão
  geral apenas pelos testes de janelas pequenas.

Os novos contadores são da execução atual. Eles não são adicionados aos
checkpoints nem reconstruídos a partir de versões anteriores. O contador
segments done mantém seu significado anterior de conclusão no agendador.

O motor e o agendador continuam com a mesma ordem e o mesmo comportamento de
busca. Esta alteração de diagnóstico não resolve o tempo gasto com o espaço
externo. Não é uma otimização nem uma correção operacional da busca de chaves.

## O que os testes anteriores não mostraram

As janelas pequenas conferiam cobertura e resultados conhecidos. Elas tinham
poucos segmentos externos, então sua conclusão não revelava a demora que aparece
quando o mesmo desenho é aplicado a um intervalo muito maior. Passar nesses
testes não validava o tempo até o primeiro trabalho útil em todas as configurações.

## Verificação dos diagnósticos

As 15 suítes passaram na compilação GCC portátil e na compilação Clang com
AddressSanitizer e UndefinedBehaviorSanitizer. O novo caso sintético confirma
as cinco unidades iniciais externas, os 49 candidatos contabilizados ao final
e os 15 descartes separados. Também verifica a mensagem para zero candidatos
e a transferência dos contadores entre a thread e o relatório.

Esses resultados validam a instrumentação adicionada, não resolvem nem encerram
a pendência de planejamento descrita acima.

## Preservação

O arquivo p71.json foi copiado sem alterações para:

    /root/excalibur369-reverse-diagnostic-kb36tao7/p71.json.preserved

SHA-256 do arquivo observado:

    b35d5d5f000d03eeb557b3e8aba64bb3b1588ad860e22dd3db6e8509200edb1c

O mesmo diretório contém a inspeção em diagnosis.json, a cópia do executável
anterior e os logs de validação dos diagnósticos. Não houve publicação no GitHub.
