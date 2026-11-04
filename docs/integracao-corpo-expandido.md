# Brisas Controladas → integração com Corpo Expandido

Implementação do produtor Linux, contrato v1 fornecido em 2026-11-03. As cópias
canônicas recebidas estão em [contrato](contracts/integracao-brisas-controladas.md)
e [JSON Schema](contracts/manifesto-gravacao-v1.schema.json). O código deste
repositório não modifica Fastify, React, o catálogo ou TouchDesigner.

## Auditoria e reaproveitamento

O checkout já continha alterações locais de captura e transferência, preservadas
nesta implementação. OpenFrameworks **0.12.1** é confirmado por `ofConstants.h`.
Addons: ofxImGui, ofxKinect, ofxOpenCv, ofxOsc, ofxTensorFlow2. O sistema dispõe de
**POCO 1.15.3**, headers e bibliotecas dinâmicas Net/Foundation; o projeto passou a
ligá-las explicitamente. `Poco::Net::WebSocket` oferece handshake cliente com
headers adicionais; ping/pong é responsabilidade do aplicativo
([documentação POCO](https://docs.pocoproject.org/current/Poco.Net.WebSocket.html)).
Não foi instalado addon obsoleto nem serviço intermediário.

| Componente existente | Comportamento preservado / adaptação |
| --- | --- |
| `GuiApp`, `MainApp`, `WindowManager` | UI/render e janelas; setup/update/exit integram o novo cliente |
| `FonteGlobalKinect` | Única captura, registro de Depth no RGB, entrega de frames na thread principal; nenhuma chamada gráfica na rede |
| `KinectRecorder` | Filas SPSC limitadas, dois encoders FFmpeg, stop assíncrono e waitpid; adicionados IDs imutáveis, nomes canônicos automáticos e persistência com fsync |
| `RecordingTransfer` | Mesmo worker serial, flock, executor posix_spawn, ffprobe, thumbnail, limite de banda, retries, logs e botão de retentativa; acrescentado protocolo v1 |
| `TransmissorSrt` | Sem alterações; rede e encoder de captura continuam separados |
| Controles manuais | Gravação Kinect manual e gravação da composição preservadas; eventos nunca adotam/param uma captura manual |

Antes, a captura usava pastas UTC e `rgb/rgb_NNN.mp4`,
`depth/depth_NNN.mp4`, `session.json`, `capture/N.json`, `timestamps.csv` e logs.
Cada segmento/canal recebia um UUID de publicação no worker; a publicação legada
enviava vídeo/JPEG, JSON temporário e renomeava JSON por SSH. Já havia persistência
de tentativas, thumbnails, validação ffprobe e recuperação de jobs. Não havia
WebSocket, vínculo explícito à sessão web ou marcador `.ready`.

Os vídeos continuam MP4 H.264/yuv420p, 640×480, CFR configurável até 30 fps,
sem áudio, faststart; Depth continua quantizado e com perdas. Nenhuma conversão
adicional ou novo pipeline de gravação foi criado.

## Configuração

Mescle [o exemplo](kinect-recording.example.json) com
`bin/data/kinect-recording.json`. Configurações existentes são preservadas.
A integração vem **desativada** porque IP, token e destino reais não foram
fornecidos. Para ativar:

```json
{
  "corpoExpandido": {
    "enabled": true,
    "url": "ws://IP_DO_MEFISTOFELES:3000/api/v1/gravador/ws",
    "tokenEnvironment": "BRISAS_GRAVADOR_TOKEN"
  },
  "transfer": {
    "enabled": true,
    "sshHost": "mephistopheles",
    "remoteDirectory": "/Users/dodo/BrisasControladas/gravacoes",
    "maxRetries": 10,
    "retryIntervalSeconds": 30,
    "bandwidthKbps": 2048
  }
}
```

`remoteDirectory` deve ser **exatamente** a pasta configurada em
`GRAVACOES_PASTA` no backend. O caminho do exemplo é o do contrato, não uma
descoberta da configuração efetiva do Mac. Não mude a pasta de uma fila em
andamento: recibos concluídos não são republicados em outro destino.

O token só é lido da variável de ambiente indicada, sem escrita em JSON, URL ou
logs. Use o mesmo valor de `GRAVADOR_TOKEN` do Fastify. Por exemplo, em Bash:

```bash
read -rsp 'Token do gravador: ' BRISAS_GRAVADOR_TOKEN
export BRISAS_GRAVADOR_TOKEN
./bin/brisascontroladas
unset BRISAS_GRAVADOR_TOKEN
```

Ao usar launcher/systemd, disponibilize a variável ao processo do aplicativo
por configuração privada do operador. Nunca versione um arquivo com o segredo.
Reinicie o aplicativo depois de alterar URL/token/configuração.

O cliente suporta `ws://` na LAN confiável ou em túnel SSH. `wss://` é recusado
explicitamente; TLS nativo não foi incluído nesta etapa. Para tráfego cifrado,
use `ssh -N -L 3001:127.0.0.1:3000 mephistopheles` e configure
`ws://127.0.0.1:3001/api/v1/gravador/ws`. O backend precisa estar ouvindo na
interface esperada; libere a porta 3000 somente para a rede necessária, conforme
o contrato. Use uma única instância do backend.

No Mac, habilite Acesso Remoto e disponibilize rsync. No Linux, use o alias SSH
existente, por exemplo:

```sshconfig
Host mephistopheles
    HostName IP_DO_MEFISTOFELES
    User dodo
    IdentityFile ~/.ssh/id_ed25519
    IdentitiesOnly yes
```

Confira a chave do host numa conexão manual. O aplicativo usa `BatchMode=yes` e
`StrictHostKeyChecking=yes`; chaves com senha precisam estar no ssh-agent.
Verifique no ambiente real:

```sh
ssh -oBatchMode=yes mephistopheles 'command -v rsync'
ssh -oBatchMode=yes mephistopheles 'mkdir -p /Users/dodo/BrisasControladas/gravacoes'
```

O host deve ser alias seguro, e o caminho absoluto ou `~/...`, sem espaços nem
metacaracteres. Essas restrições já existiam no executor. Os processos locais
recebem argv separado; nenhum dado recebido pelo WebSocket vira comando shell.

## Comunicação e estados

```text
Fastify → POCO / thread de rede
        → validação + checkpoint persistido + fila limitada
        → GuiApp::update / SessionRecordingController
        → KinectRecorder::start/stop / worker FFmpeg
        → session.json completed após waitpid dos dois encoders
        → RecordingTransfer / worker serial
        → ffprobe + JPEG opcional + manifesto → rsync → .ready
        → importador do Corpo Expandido → gravacao.atualizada → React
```

`SessionWebSocketClient` autentica com `Authorization: Bearer ...`, recebe
fragmentos de texto até 16 KiB, responde ping com pong e aguarda estado inicial.
Detecta close/EOF/erro, ausência de estado em 10 s e ausência de tráfego/heartbeat
em 75 s. Reconexão em 1, 2, 5, 10, 30 s; o contador reinicia após um evento
válido. HTTP 401/403 é identificado nos logs/status; 409 indica conflito de
sessões no backend. Não há comandos Linux → backend, ACK ou replay.

`SessionProtocol` aceita exatamente `v`, `type`, `eventId`, `sessaoId`,
`timestamp`. v é inteiro 1; IDs UUID minúsculos; calendário UTC válido, fração
opcional de 1–3 dígitos. Só `sessao.estado` aceita sessão nula. São suportados:

- `sessao.iniciada`: reserva a captura para a sessão, aguarda Kinect e ausência de
  captura manual e inicia com UUID persistido.
- `sessao.encerrada`: altera somente a sessão correspondente; pede stop.
- `sessao.estado`: mesma sessão mantém a captura; nulo finaliza a automática;
  sessão diferente aguarda fechamento completo antes de iniciar outra.

O checkpoint `session-control.json` mantém maior timestamp aceito, últimos 256
IDs de evento, sessão desejada, identidade da captura e sessões encerradas.
Mensagens anteriores ao timestamp persistido são ignoradas; empates seguem a
ordem do socket, e uma sessão encerrada não reabre por `iniciada` repetido.
O UUID é reutilizado enquanto representa a mesma captura. `session.json`
concluído/erro impede que uma nova sincronização reinicie essa captura.
Mantenha relógios sincronizados: rollback no relógio do backend pode causar
rejeição de eventos até o timestamp alcançar o último aceito.

A UI consome uma fila protegida por mutex. DNS, sockets, persistência dos eventos,
FFmpeg, ffprobe, fsync e rsync rodam fora de update/draw. O shutdown pode aguardar
os timeouts de rede/processos; não se espera rede durante renderização.

**Queda do WebSocket não encerra captura.** Esta é uma decisão explícita do pedido
que prevalece sobre a recomendação contrária no contrato anexado. Após reconectar,
o snapshot é reconciliado. Sessões inteiras ocorridas offline não são recuperáveis.

Uma captura manual existente adia a automática enquanto a sessão continuar
ativa. Durante uma automática, o botão de iniciar outra captura não aparece.
O botão Stop local permanece como interrupção do produtor: não encerra a sessão
web e não reinicia automaticamente a mesma captura. Desconexão do Kinect, limite
de armazenamento/duração e erros dos encoders continuam podendo parar a captura.
A UI informa o modo, a sessão, o UUID e o estado de conexão/transferência.

## Arquivos e manifesto

Apenas capturas automáticas usam a estrutura nova, sem copiar os vídeos:

```text
recordings/<gravacaoId>/
  <gravacaoId>-rgb000.mp4
  <gravacaoId>-depth000.mp4
  <gravacaoId>-rgb001.mp4   # se houver outros segmentos
  <gravacaoId>-depth001.mp4
  <gravacaoId>.jpg          # opcional
  <gravacaoId>.json         # manifesto canônico
  session.json             # diagnóstico técnico local
  capture/0.json           # intervalos reais da captura
  rgb/segments.csv
  depth/segments.csv
  timestamps.csv
  publication/automatic.job.json
  publication/<gravacaoId>.ready
```

Um UUID por captura completa, compartilhado entre os canais e preservado em
retries. Não há UUID novo por envio. RGB vem primeiro na lista do manifesto;
Depth e segmentos seguintes são arquivos adicionais. Os metadados originais
continuam locais, pois o manifesto canônico proíbe campos extras.

O manifesto tem somente `v`, `gravacaoId`, `sessaoId`, `iniciadaEm`, `encerradaEm`
e `arquivos[{nome,tipo,tamanhoBytes}]`. Horários vêm dos eventos de captura,
antes da espera de encoder/transferência, e usam UTC com milissegundos. Os nomes
são basenames canônicos; tamanhos reais positivos são medidos após fechamento.
JPEG é omitido quando desativado, e falha ao gerar JPEG habilitado fica pendente.

O contrato limita 32 entradas. Para a captura automática, o intervalo de segmento
é `max(segment_seconds, ceil(max_duration_seconds / 15))`: até 30 MP4 + JPEG.
Com os padrões de quatro horas e 600 s, resulta em segmentos de **960 s**.
Captura manual mantém seus 600 s. O limite de quatro horas/10 GB permanece.
O primeiro MP4 RGB é o vídeo principal servido pelo backend; os demais devem ser
acessados pelas rotas de arquivos adicionais, não por concatenação implícita.

## Transferência, confirmação e recuperação

A fila automática espera `session.json` completed e exit code zero dos **dois**
encoders, escritos após waitpid e fsync dos MP4 automáticos. Em seguida verifica cada segmento com ffprobe
(codec, duração e número de frames contra eventos da captura), cria e sincroniza o JPEG opcional,
grava o manifesto por temporário + fsync + rename + fsync do diretório e fixa seu
SHA-256 no job. Arquivos canônicos devem ser regulares, sem symlinks/hard links.

1. Valida identidade, nomes e tamanhos. Recusa manifesto preparado alterado.
2. Consulta `.ready` remoto. Hash diferente é colisão; hash igual recupera uma
   publicação cuja confirmação local se perdeu, sem reescrever mídia.
3. Envia cada MP4, JPEG opcional e JSON com rsync `--delay-updates`, aguardando
   sucesso em cada chamada. Nunca usa `--inplace`, `--append` ou `--delete`.
4. Gera marcador numa pasta separada, contendo SHA-256 hexadecimal minúsculo
   **dos bytes exatos do JSON**, mais newline.
5. Envia `.ready` em outra chamada rsync. A renomeação temporário→final do rsync
   publica o marcador no filesystem de destino.
6. Só depois persiste `state=completed` no job local.

A pasta remota é plana e contém somente os arquivos declarados, JSON e ready.
O importador deve exigir o marcador, nunca apenas a presença do JSON. Não se
utilizam os diretórios staging/ready sugeridos originalmente: o contrato
canônico escolheu marcador explícito. O SHA-256 não é checksum dos vídeos.

Captura (`starting/recording/completed/error` mais status de finalização) e
transferência (`pending/transferring/publishing/completed/failed`) têm ciclos
separados. Nova captura não espera rede. Um worker e o flock por output_root
serializam a fila; o cliente de controle também tem lock exclusivo por root.

Em falha, preservam-se mídia, manifesto, UUID, contagem e próxima tentativa. No
reinício, trabalhos preparados ou concluídos sem recibo retomam; retries repetem
os mesmos bytes, incluindo o caso de falha ao publicar ready. Configuração de
thumbnail alterada não modifica jobs já preparados. O botão **Tentar transferências
novamente** zera o esgotamento das tentativas, sem gerar outra gravação.

Não edite um manifesto/arquivo já publicado. Se a validação detectar tamanho ou
hash diferente, restaure os originais antes de repetir. Nenhum arquivo local é
apagado automaticamente. Consulte `transfer.log`, `transfer-process.log`, os logs
do encoder e `publication/automatic.job.json` para o motivo da falha.

Uma captura interrompida por morte do produtor, sem fechamento confirmado, ganha
`interrupted.json` de diagnóstico e não é publicada, mesmo se ffprobe conseguir
abri-la. Requer inspeção/recuperação manual; não transforme `state` em completed
apenas para forçar envio. Após estado oficial ativo e detecção de produtor anterior
morto, a nova captura recebe outro UUID; isso também permite continuar uma sessão
após encerramento normal do aplicativo (`application_exit`), sem reutilizar a
captura já concluída. Nos demais encerramentos locais intencionais, a mesma
captura não reinicia. O material interrompido permanece local.
A detecção do produtor usa PID, boot ID e instante de criação do processo em
`/proc`, evitando confundir um PID reutilizado após reboot com o gravador anterior.
Checkpoint corrompido bloqueia o controle remoto; não é descartado silenciosamente.

## Testes sem Kinect

Dependências: compilador C++17, pthread, POCO Net/Foundation, nlohmann/json já
incluído no OF, Python 3 (somente stdlib), FFmpeg/libx264, ffprobe, rsync.

```sh
bash tests/run_session_integration.sh
bash tests/run_kinect_recorder.sh
bash tests/run_recording_transfer.sh
make -j2
make -f tests/Makefile TestReleaseABI -j2
```

O teste novo usa sockets POCO reais contra fixture HTTP/WebSocket local e
FFmpeg/ffprobe/rsync reais. SSH é simulado para executar no diretório temporário,
sem tocar no Mac. Verifica Bearer, 401, fragmentação, pong, desconexão/reconexão,
JSON inválido/antigo/duplicado, checkpoint após reinício, conflito manual,
transições de sessão, manifesto usando as regras do schema fornecido, decodificação
dos vídeos, JPEG opcional, rsync falhando na mídia/ready, perda de confirmação,
colisão remota, arquivo local alterado e captura interrompida.
Artefatos ficam em `/tmp/brisas-session-*`; as suites anteriores continuam cobrindo
captura manual, encoder morto/travado, armazenamento e publicação legada.

## Pendências no ambiente real

- Configurar URL, token compartilhado, alias SSH e pasta equivalente a
  GRAVACOES_PASTA; ativar `corpoExpandido.enabled`.
- Validar handshake com a instância Fastify real (incluindo 409), evento de início
  e encerramento, recebimento/importação e associação pelo sessaoId.
- Confirmar `gravacao.atualizada` e atualização React, inclusive acesso aos canais
  e segmentos adicionais; esse repositório não implementa o consumidor.
- Exercitar Kinect físico + UI/OpenGL + SRT simultâneo, consumo de CPU/disco/rede,
  gravação longa e reprodução no TouchDesigner. Os testes sintéticos não comprovam
  desempenho da instalação nem a ingestão efetiva no Mac.

## Arquivos desta integração

Novos: `IntegrationSupport.h`, `SessionProtocol.{h,cpp}`,
`SessionWebSocketClient.{h,cpp}`, `SessionRecordingController.h`, três arquivos
`tests/*session_integration*`, este guia, exemplo JSON e cópias do contrato/schema.
Adaptados: `KinectRecorder.{h,cpp}`, `RecordingTransfer.cpp`, `GuiApp.{h,cpp}`,
`config.make`, configuração Kinect, script de teste da transferência, README e
referências nos guias existentes. `FonteGlobalKinect` e `TransmissorSrt` não
precisaram de mudanças adicionais nesta integração.

## Validação nesta entrega — 2026-11-03

- `make -j2`: passou; permanecem avisos preexistentes em addons/UI.
- `bash tests/run_recording_transfer.sh`: passou, incluindo publicação legada,
  rede indisponível, retomada e arquivos inválidos. Artefatos finais:
  `/tmp/brisas-transfer-lf2_yxak`.
- `bash tests/run_session_integration.sh`: passou com POCO/FFmpeg/rsync reais e
  fixtures locais, incluindo recuperação do controle e da publicação, fsync da
  mídia e identificação do produtor após reboot. Artefatos finais:
  `/tmp/brisas-session-iflbyjgr`.
- `bash tests/run_kinect_recorder.sh`: passou, incluindo decodificação, limites e
  falhas dos encoders. Artefatos: `/tmp/brisas-recorder-test.6eM8zF`.
- `make -f tests/Makefile TestReleaseABI -j2` e execução de
  `obj/linux64/Release/tests/lifecycle` sem `--gl`: passaram.
- A primeira execução da fixture WebSocket foi bloqueada pelo sandbox ao criar
  socket. A execução com permissão para localhost passou, sem acessar o Mac.
  O teste confirma que a captura real do gravador permanece ativa na desconexão.
- Os módulos de teste são compilados com `-Wall -Wextra -Werror`.
- Não foram executados testes no Fastify real, no React, no Mac por SSH real,
  no Kinect físico, com OpenGL, nem no TouchDesigner.
