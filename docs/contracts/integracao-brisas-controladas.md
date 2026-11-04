# Corpo Expandido ↔ Brisas Controladas — contrato v1

Esta implementação cobre o backend e o frontend do Corpo Expandido. **O cliente C++ ainda precisa ser implementado no outro repositório.** Não há validação ponta a ponta com Kinect, rsync real ou TouchDesigner nesta entrega.

## Auditoria e decisões

O backend usa Fastify/TypeScript, `better-sqlite3`, WAL, foreign keys e migrações SQL transacionais numeradas. `sessoes` e `participantes_sessao` guardam estado e snapshots das pessoas. `repositorioSessoes` já inicia/encerra em transações imediatas; encerramento repetido já era idempotente. O domínio legado permite várias sessões abertas, confirmado nos testes. Não se fecha nenhuma sessão automaticamente.

`gravacoes` já tem UUID próprio, JSON privado e FKs de sessão/pessoa. Não existe unicidade por sessão e ela não foi adicionada. `decisoes_gravacao` registra alterações na mesma transação do catálogo. `eventos` guarda o envelope publicado. Essas estruturas foram reaproveitadas; campos opcionais no JSON dispensam nova migração SQL e históricos anteriores continuam legíveis. As migrações 001–004 permanecem sem alteração nesta etapa.

O catálogo já varria a pasta na inicialização e periodicamente, independentemente do navegador; não era preciso criar outro importador. O recebimento legado usa `<uuid>.mp4/.jpg/.json`; JSON publicado por último é seu sinal de conclusão. Exigia JPEG e associava por sobreposição temporal, com decisões provisórias e ambiguidade. Não havia `sessaoId` explícito nem endpoint autenticado do gravador. Arquivos inválidos eram reabertos a cada varredura. A biblioteca tinha eventos e polling, mas os detalhes de sessão não mostravam gravações.

A implementação mantém o leitor legado e sua heurística **apenas para manifestos legados**. O manifesto v1 tem associação exclusiva pelo `sessaoId`, sem inferir autoria do Kinect a partir dos participantes. Um ID desconhecido fica em erro pendente de resolução, com `sessaoManifestoId` preservado e `sessaoId` nulo na FK. Nunca é associado à sessão mais recente. Associação manual do formato v1 é recusada para não contradizer o contrato do produtor.

Não há FFmpeg nem geração de thumbnail no projeto; nenhuma dependência foi acrescentada. Tipos aceitos são MP4 e JPEG, sem inspeção de codec/conteúdo. Caminhos privados não são URLs públicas. As rotas de mídia existentes mantêm Range, HEAD e validação de tamanho; arquivos adicionais recebem rotas específicas.

## Configuração e rede

```dotenv
HOST=192.168.1.10
PORTA=3000
GRAVADOR_TOKEN=SEGREDO_ALEATORIO_COMPARTILHADO_COM_NABERIUS
GRAVACOES_PASTA=/Users/dodo/BrisasControladas/gravacoes
GRAVACOES_INTERVALO_MS=5000
```

Substitua HOST pelo IP privado real da interface do Mephistopheles. O padrão permanece `127.0.0.1`; `0.0.0.0` inclui todas as interfaces e só deve ser usado se desejado. Não configure encaminhamento de porta no roteador. Restrinja entrada no firewall/rede à LAN necessária. APIs públicas existentes não têm login: o token autentica somente o endpoint do dispositivo. CORS não é autenticação. Em rede não confiável, use túnel/reverse proxy TLS e `wss`.

`GRAVADOR_TOKEN` pertence somente ao backend e ao cliente C++; não criar variável `VITE_*`, query string ou log contendo o token. Sem token, o endpoint responde 401. O backend nunca executa comandos recebidos pelo canal do gravador.

Com token configurado, iniciar sessão é exclusivo dentro da transação SQLite: repetir os mesmos participantes na mesma ordem retorna a sessão ativa sem emitir outro evento; outros participantes recebem 409 até encerrá-la. O retorno HTTP existente continua 201. Sem token, o modo legado com sessões simultâneas é preservado. Se há mais de uma sessão legada aberta, o handshake do gravador retorna 409; o operador deve encerrá-las até restar no máximo uma.

## WebSocket do gravador

```
GET ws://IP_DO_MEFISTOFELES:3000/api/v1/gravador/ws
Authorization: Bearer <GRAVADOR_TOKEN>
```

Sem credencial ou credencial incorreta: HTTP 401, antes do upgrade. Cliente C++ deve enviar o header no handshake. Não há Origin usado como autenticação. Payload máximo WebSocket: 16 KiB; clientes lentos com buffer acima de 1 MiB são desconectados; ping/pong de transporte a cada 30 s detecta conexões mortas.

Todas as mensagens têm exatamente os campos abaixo; IDs são strings UUID canônicas minúsculas, timestamps UTC, `v = 1`:

```json
{"v":1,"type":"sessao.iniciada","eventId":"1d48dbb5-fbd3-4dac-819a-73c3b5f91324","sessaoId":"33333333-3333-4333-8333-333333333333","timestamp":"2026-11-03T21:18:00.000Z"}
{"v":1,"type":"sessao.encerrada","eventId":"2d48dbb5-fbd3-4dac-819a-73c3b5f91324","sessaoId":"33333333-3333-4333-8333-333333333333","timestamp":"2026-11-03T21:20:00.000Z"}
{"v":1,"type":"sessao.estado","eventId":"3d48dbb5-fbd3-4dac-819a-73c3b5f91324","sessaoId":null,"timestamp":"2026-11-03T21:21:00.000Z"}
```

`iniciada` e `encerrada` são emitidos após commit, com o timestamp persistido da transição. Falha na transação não emite evento. `estado` é enviado em toda conexão, consultando SQLite, com ID da sessão ativa ou null. Consulta, inscrição e envio inicial são síncronos no mesmo event loop, sem await entre eles; eventos posteriores ficam na ordem do socket. O serviço deve rodar como **uma instância do backend**; não há barramento entre processos.

Não há ACK/replay durável. Queda entre commit e envio pode perder a notificação; reconexão recupera o estado atual. O cliente deve deduplicar `eventId` e operações por sessão. `estado` com o mesmo ID não deve iniciar outra gravação; ID diferente deve finalizar a gravação local anterior e iniciar a atual; null deve finalizar qualquer captura vinculada que permaneceu aberta. Uma sessão inteira ocorrida enquanto o Linux estava offline não pode ser capturada retroativamente.

Na perda de conexão, recomenda-se finalizar e preservar o segmento local, reconectar com backoff e consultar o estado. Se retomar a mesma sessão, criar **outro `gravacaoId`**. Várias gravações na mesma sessão são válidas. Comandos de encerramento enviados pelo Linux são ignorados.

## Manifesto canônico v1

Arquivo UTF-8 `<gravacaoId>.json`, até 64 KiB, sem campos extras. Esquema legível por máquina: [manifesto-gravacao-v1.schema.json](manifesto-gravacao-v1.schema.json).

```json
{
  "v": 1,
  "gravacaoId": "11111111-1111-4111-8111-111111111111",
  "sessaoId": "33333333-3333-4333-8333-333333333333",
  "iniciadaEm": "2026-11-03T21:18:00.000Z",
  "encerradaEm": "2026-11-03T21:20:00.000Z",
  "arquivos": [
    {"nome":"11111111-1111-4111-8111-111111111111.mp4","tipo":"video","tamanhoBytes":12345678},
    {"nome":"11111111-1111-4111-8111-111111111111.jpg","tipo":"thumbnail","tamanhoBytes":34567}
  ]
}
```

- `gravacaoId`: UUID único por captura/segmento, igual ao basename do JSON e do marcador. Imutável depois da validação bem-sucedida.
- `sessaoId`: UUID recebido do backend, copiado literalmente. O exemplo genérico `"123"` não é válido neste domínio, cujas sessões são UUIDs.
- Datas: UTC terminado em Z, fração opcional de 1–3 dígitos, calendário válido e início estritamente anterior ao fim.
- `arquivos`: de 1 a 32 entradas; ao menos um vídeo MP4, no máximo uma thumbnail JPEG; thumbnail ausente é permitida. A primeira entrada de vídeo é o vídeo principal das rotas existentes.
- `tamanhoBytes`: obrigatório nesta versão, inteiro positivo seguro em JavaScript (máximo 9007199254740991), igual ao arquivo.
- Nomes: `<gravacaoId>.mp4`, `<gravacaoId>.jpg`, ou `<gravacaoId>-<sufixo>.mp4/.jpg`, com sufixo de 1–16 caracteres `a-z0-9`. Exemplo: `<id>-02.mp4`. Sem subdiretórios, barras, `..`, caminhos absolutos ou nomes compartilhados entre IDs. Não aceitamos o nome genérico `gravacao.mp4` no diretório plano para impedir colisões entre gravações.
- Tipo e extensão precisam concordar. Entradas e nomes duplicados são recusados. Symlinks e hard links nos arquivos canônicos são recusados (`O_NOFOLLOW`, arquivo regular, um link). A pasta raiz é configurada pelo operador e resolvida para caminho real.

## Transferência e confirmação explícita

Usamos diretório de recebimento plano para preservar o catálogo e as rotas existentes. O marcador `<gravacaoId>.ready` é um arquivo ASCII contendo **somente o SHA-256 hexadecimal minúsculo dos bytes exatos do JSON**, com newline final opcional. Ele não é checksum dos vídeos. Só é publicado depois de todos os arquivos fechados e de rsync bem-sucedido.

No Linux, preparar cada gravação numa pasta exclusiva e persistir a fila de transferências. Enviar mídia e JSON primeiro com `rsync --delay-updates`; evitar `--inplace`, `--append`, `--delete` e retransmissões concorrentes do mesmo ID. Depois do código de saída zero, gerar o marcador em uma pasta local separada e transferi-lo em uma segunda chamada. O rsync padrão escreve temporário e renomeia no destino; `--delay-updates` adia a publicação dos arquivos desse lote. **Nunca incluir `.ready` no primeiro lote**.

Exemplo executável no Linux, assumindo captura já finalizada e manifesto com tamanhos reais:

```sh
id=11111111-1111-4111-8111-111111111111
captura="/var/lib/brisas/fila/$id"
marcadores="/var/lib/brisas/marcadores/$id"
destino="dodo@IP_DO_MEFISTOFELES:/Users/dodo/BrisasControladas/gravacoes/"
mkdir -p "$marcadores"
# A pasta captura contem apenas JSON e arquivos declarados. Nao contem .ready.
rsync -av --delay-updates "$captura/" "$destino" || exit 1
sha256sum "$captura/$id.json" | cut -d ' ' -f 1 > "$marcadores/$id.ready"
rsync -av --delay-updates "$marcadores/$id.ready" "$destino" || exit 1
```

Use chave SSH do produtor com permissões apropriadas e diretório de destino existente. Não construir shell com nomes arbitrários de manifestos: o C++ deve gerar UUIDs e basenames conforme contrato e invocar processos com argumentos separados. O backend não executa rsync nem comandos vindos dos metadados.

Uma alternativa compatível é enviar para staging e, no Mac, renomear os arquivos e por último o marcador no mesmo filesystem. Renomear apenas um MP4 não confirma a gravação inteira. Estabilidade de tamanho nunca substitui o marcador.

Retransmissão após sucesso deve manter **os mesmos bytes do manifesto e os mesmos arquivos**. Alteração de metadados de gravação já validada é colisão, fica indisponível até restaurar o contrato original. Para outra captura, gere novo ID. Um manifesto nunca validado pode ser corrigido e reenviado. Não remover marcador de uma gravação disponível para reescrever seu conteúdo; não usar o mesmo ID para capturas diferentes.

## Ingestão, estados e recuperação

O catálogo usa `fs.watch` nativo para antecipar chegadas, mais varredura inicial e periódica para recuperar eventos perdidos. Falha no watcher não impede o polling. As varreduras são serializadas, usam filesystem assíncrono e encerram antes do banco. Nenhuma página precisa estar aberta.

`status` histórico de associação continua existindo (`associada`, `pendente`, `ambigua`, `invalida`). O formato v1 acrescenta `aguardando_transferencia`, `processando`, `erro` e usa `disponivel` depois da validação/associação. O campo público `disponibilidade` separa disponibilidade da mídia do estado da sessão e da associação legada. Não há registro artificial de gravação enquanto seu UUID não é conhecido: sessão encerrada sem gravações mostra espera na interface. `recebendo` não é inferido por tamanho. Sessão ativa mostra “Sessão em andamento”, sem afirmar que o Linux confirmou captura.

1. JSON v1 válido sem marcador: `aguardando_transferencia`, URLs nulas.
2. Marcador correspondente + sessão existente: persiste `processando`, depois valida todos os arquivos.
3. Validação completa: salva identidade, sessão, arquivos e `disponivel` na estrutura existente e publica evento.
4. Falha: `erro`, URLs nulas, motivo público sem caminho local; associação explícita conhecida é preservada para mostrar o erro na sessão.
5. Sessão inexistente: FK nula, ID solicitado preservado, sem heurística ou correção manual arbitrária. Corrigir manifesto ainda não validado ou restaurar a sessão legítima.

Sem manifesto, arquivos ficam no filesystem e geram aviso estruturado, sem criar associação. JSON truncado ou inválido nunca libera mídia. Marcador sem JSON gera erro recuperável. Após restart, registros em processamento e arquivos concluídos offline são revalidados. Reenvios idênticos não criam duplicatas nem novos eventos de disponibilidade. A PK por UUID e a transação do histórico garantem identidade persistida.

Falhas usam backoff exponencial de 2 s até 5 min, com antecipação quando tamanho/mtime/inode dos arquivos conhecidos muda. Backoff fica em memória e reinicia com o processo; o erro e o histórico ficam no banco. Erros sem mudança não criam novas decisões repetidas. Não há política de exclusão automática; corrigir os arquivos/contrato ou resolver a sessão e aguardar reconciliação. O scanner não move nem sobrescreve arquivos.

## Frontend e mídia

O canal público `/api/v1/eventos` mantém o envelope usado pelo TouchDesigner:

```json
{"tipo":"gravacao.atualizada","versao":1,"eventoId":"UUID","ocorridoEm":"2026-11-03T21:22:00.000Z","dados":{"gravacao":{"id":"11111111-1111-4111-8111-111111111111","sessaoId":"33333333-3333-4333-8333-333333333333","status":"disponivel","disponibilidade":"disponivel"}}}
```

Exemplo abreviado; `dados.gravacao` contém a representação pública completa. `gravacao.descoberta` é a primeira persistência; `gravacao.atualizada` cobre transições seguintes. São enviados após persistir o catálogo. Uma queda entre commit e publicação ainda pode perder evento: reconexão e polling HTTP recuperam o estado; não se promete entrega exatamente uma vez.

TanStack Query invalida todas as consultas `['gravacoes', ...]` nos eventos e na reconexão. Biblioteca e detalhes da sessão também consultam a cada 15 s. A sessão encerrada atualiza sem reload. Thumbnail só é renderizada quando há URL; vídeo só quando importado. O backend continua validando acesso mesmo se uma URL antiga tiver sido copiada.

Rotas mantidas: `/api/v1/gravacoes`, `/:id`, `/:id/video`, `/:id/thumbnail`, `/:id/associacao` (legadas). Nova rota: `/:id/arquivos/:nome`, restrita à lista validada. MP4 usa `video/mp4`, JPEG `image/jpeg`, `nosniff`, `no-store`, Range e HEAD. Não há exposição estática da pasta ou de JSON/marcadores. Manifesto e marcador canônicos são conferidos novamente ao servir mídia; tamanho do arquivo é conferido no descritor aberto.

## Mudanças necessárias no C++

- Implementar cliente WS persistente com Bearer, ping/pong, reconexão e deduplicação.
- Reconciliar estado local com `sessao.estado`, inclusive ID nulo ou alterado.
- Iniciar/parar gravação pelos eventos, sem enviar comandos de encerramento ao backend.
- Guardar `sessaoId` por captura e gerar UUID próprio por gravação/segmento.
- Fechar e descarregar todos os arquivos antes de escrever manifesto e tamanhos reais.
- Produzir MP4 reproduzível no navegador e JPEG opcional; não há conversão no Mac.
- Persistir fila local e repetir transferências com mesmos IDs após falha/restart.
- Transferir conteúdo e JSON, verificar exit code, só então publicar `.ready` correspondente.
- Tratar UUIDs/arquivos publicados como imutáveis e não executar dois uploads simultâneos do mesmo ID.

O fluxo legado continua disponível durante a migração conforme [gravacoes.md](gravacoes.md), mas só é seguro quando o produtor já publica seu JSON por último. Não é possível deduzir com garantia que uma transferência antiga terminou sem essa cooperação. Migrar o C++ para v1 para obter associação explícita e confirmação vinculada ao manifesto.
