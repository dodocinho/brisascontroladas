# Controle de sessões do Corpo Expandido

O Linux/Naberius recebe eventos do backend no Mac/Mephistopheles por WebSocket.
A integração reutiliza a captura Kinect, os dois encoders de gravação, o transmissor
SRT da composição e o worker de transferência existentes. Não requer novo addon
ou pacote: usa POCO Net/Foundation e nlohmann JSON já ligados ao projeto.

## Arquitetura investigada

| Componente | Responsabilidade |
| --- | --- |
| `FonteGlobalKinect` | Singleton atualizado por `GuiApp::update`, câmera RGB/depth compartilhada, demonstração quando indisponível; entrega frames ao gravador |
| `KinectRecorder` | Filas SPSC limitadas; worker com dois FFmpeg; MP4/H.264, depth quantizado em 8 bits, segmentação, limites de tamanho/duração, fechamento com waitpid |
| `TransmissorSrt` | Envia a composição da janela de projeção (800×600), caller SRT para listener no Mac; threads de escrita e leitura de stderr, retentativas existentes |
| `SessionWebSocketClient` | Thread POCO: conexão, registro, JSON, persistência e filas protegidas; não toca câmera, encoder ou OpenGL |
| `SessionRecordingController` | Executado na thread principal: sessão desejada, gravação automática e propriedade da transmissão |
| `RecordingTransfer` | Worker serial existente: ffprobe, thumbnail, manifesto, rsync, retries persistentes e publicação por `.ready` |

As duas janelas compartilham a thread principal. O start do gravador é assíncrono;
a solicitação do SRT acontece no mesmo update, sem temporizador entre eles.
O início efetivo dos dois encoders/pipelines pode diferir: não há promessa de
sincronização de exposição entre Kinect, composição e receptor SRT.

## Configuração no Naberius

Mescle [o exemplo](kinect-recording.example.json) com
`bin/data/kinect-recording.json`, mantendo os ajustes de gravação/transferência:

```json
{
  "corpoExpandido": {
    "enabled": true,
    "url": "ws://IP_DO_MEPHISTOPHELES:3000/api/v1/eventos",
    "tokenEnvironment": "",
    "tokenFile": "corpo-expandido.token"
  },
  "srt": {
    "port": 9494,
    "latencyMs": 20
  }
}
```

Substitua o placeholder pelo IPv4 real do Mac. A URL não está fixada no código.
O IPv4 da URL alimenta o campo IP do Mac da interface, também usado pelo SRT.
Se usar nome DNS ou túnel para WebSocket, adicione `srt.host` com o IPv4 real do
receptor. Porta e latência são configuráveis também na interface. Reinicie o
aplicativo após editar o arquivo; o controle IP da interface conserva o fluxo
existente de atualizar/persistir o host WebSocket.

O backend exige `Authorization: Bearer <GRAVADOR_TOKEN>` para registrar o
openFrameworks. Crie `bin/data/corpo-expandido.token` com **apenas o valor** de
`GRAVADOR_TOKEN` configurado no Mac (sem aspas nem `GRAVADOR_TOKEN=`), e execute
`chmod 600 bin/data/corpo-expandido.token`. Esse arquivo é ignorado pelo Git.
O caminho relativo de `tokenFile` é resolvido dentro de `bin/data`.

Como alternativa, remova `tokenFile`, configure
`"tokenEnvironment": "CORPO_EXPANDIDO_TOKEN"` e disponibilize a variável ao
processo que inicia o aplicativo. O conteúdo deve ser o mesmo segredo do Mac;
`CORPO_EXPANDIDO_TOKEN` é somente o **nome da variável**. Um export em outro
terminal não é herdado por um aplicativo iniciado pelo desktop.

Configure somente uma das fontes. Ausência de credencial impede a conexão;
não há fallback sem autenticação. O arquivo é relido a cada tentativa, e o
segredo não aparece nos logs. Suporte atual: `ws://` em LAN/túnel SSH;
`wss://` é recusado.

O exemplo versionado vem desativado até configurar o destino. O arquivo local
ignorado pelo Git não é substituído. O cliente usa somente o protocolo atual:
troque URLs antigas por `/api/v1/eventos` e remova a chave obsoleta `protocol`.

A transferência conserva `transfer.enabled`, `sshHost`, `remoteDirectory`,
`maxRetries`, `retryIntervalSeconds` e `bandwidthKbps`. O diretório remoto deve
coincidir com a pasta de ingestão do backend. Consulte também
[transferência](transferencia-kinect.md) e [gravação](gravacao-kinect.md).

## Protocolo de eventos

Após **cada** conexão, antes de qualquer estado, o cliente envia:

```json
{"tipo":"cliente.registrar","cliente":"openframeworks","versao":1}
```

O backend atual envia um `sessao.estado` público ao abrir o socket, antes de
avaliar o registro. Após autorizar a credencial, envia outro snapshot para o
dispositivo. O cliente aguarda esse segundo snapshot antes de controlar captura
ou enviar estados. Receber apenas o primeiro **não confirma autenticação**.
Sem confirmação em dez segundos, reconecta com backoff.

Exemplo de início após o registro:

```json
{
  "tipo": "sessao.iniciada",
  "versao": 1,
  "sessao": {
    "id": "identificador-da-sessao",
    "iniciadaEm": "2026-11-11T18:00:00.123Z",
    "corpoPresente": {
      "participanteId": "participante-123",
      "elemento": "ar",
      "corPrincipal": "#00aaff",
      "paleta": ["#00aaff", "#112233"]
    },
    "corpoExpandido": null
  }
}
```

`versao` deve ser inteiro 1. `id` é texto não vazio, até 256 bytes, sem caracteres
de controle; nunca vira caminho ou comando. `iniciadaEm` usa UTC ISO 8601 com
`Z` e precisão opcional de 1–3 casas. `corpoPresente` é objeto; seus campos são
opcionais, strings, e `paleta` é lista de strings. `corpoExpandido` é objeto
opcional ou nulo. Campos adicionais são preservados nos metadados locais.
Mensagens inválidas são rejeitadas e registradas sem interromper a captura.

Encerramento:

```json
{"tipo":"sessao.encerrada","sessaoId":"identificador-da-sessao"}
```

`versao: 1` é opcional no encerramento. Um ID diferente não afeta a sessão ativa.

Sincronização: `{"tipo":"sessao.estado","versao":1,"sessao":{...}}` usa o mesmo
objeto do início; `sessao: null` finaliza somente a gravação automática existente.
O backend deve enviar o snapshot autoritativo após cada registro, inclusive nulo.
Como esse contrato não fornece sequência/eventId, aplica-se a ordem de chegada
no socket; `iniciadaEm` não é usado como ordem de eventos. Inícios de sessões já
encerradas são ignorados; um snapshot explícito continua sendo autoritativo.

## Estados, recuperação e controles manuais

- Uma sessão tem UUID de captura persistido antes de chegar à UI. Repetir início
  ou snapshot da mesma sessão não cria encoders nem reinicia arquivos.
- Uma gravação manual adia a automática. A integração não adota nem encerra a
  gravação manual. O botão Stop da gravação automática continua disponível;
  snapshots repetidos no mesmo processo não desfazem esse Stop.
- Ao iniciar uma nova sessão, a thread principal solicita abrir o Kinect caso
  esteja desconectado. Faz uma tentativa por sessão, sem loop que sobreponha o
  controle manual. Falha de captura/encoder aparece nos estados enviados.
- Se o SRT já estava solicitado, ele é reutilizado e permanece ligado ao fim da
  sessão. Caso a sessão o tenha iniciado, ele para após o gravador fechar os
  arquivos. **Manter transmissão após sessão** transfere o controle à operação
  manual. Stop manual suprime reinício por mensagens duplicadas da mesma sessão.
- Queda do WebSocket **não** para captura ou transmissão. Backoff: 1, 2, 5, 10,
  30 segundos; o contador reinicia somente após conexão registrada estável por
  pelo menos 30 segundos. Credencial ausente, HTTP 401/403 ou fechamento 1008
  aguardam 30 segundos antes da próxima tentativa. Ping a cada 20 segundos,
  timeout sem tráfego de 75 segundos, mensagens/fragmentos limitados a 16 KiB.
- No reinício do aplicativo, um snapshot ativo permite nova captura com outro
  UUID. Os arquivos anteriores nunca são sobrescritos. Capturas interrompidas
  sem confirmação de fechamento são preservadas e não são publicadas.
- Uma mudança para outra sessão aguarda finalizar os arquivos da anterior. A
  transferência não bloqueia a nova gravação. Nunca se desliga o Kinect ao fim
  de uma sessão.

O cliente verifica o estado local até cinco vezes por segundo e envia
`dispositivo.estado` somente quando muda, ou após novo registro. Só envia estados
da sessão informada pelo backend ou o encerramento dessa sessão. Não envia
telemetria com ID nulo, pois o backend a rejeita. A fila de saída mantém somente
o snapshot mais recente (estados intermediários muito rápidos podem ser combinados):

```json
{
  "tipo": "dispositivo.estado",
  "versao": 1,
  "cliente": "openframeworks",
  "sessaoId": "identificador-da-sessao",
  "gravando": true,
  "transmitindo": false,
  "finalizando": false,
  "modo": "automatico",
  "mensagem": "Automática: sessão=... gravação=..."
}
```

`erro` contém diagnóstico quando houver falha. `gravando` exige que o encoder
esteja aceitando frames e já tenha escrito pares. `transmitindo` exige progresso
de saída do FFmpeg (`-progress`), além de processo ativo; abrir um processo ou
escrever no pipe de entrada não basta. Isso confirma saída no produtor, não
confirma renderização no TouchDesigner. Uma gravação manual continua disponível
na interface, mas seu estado com `sessaoId: null` não é enviado ao backend.
No fechamento automático, o ID é conservado no estado final com `gravando: false`.
O backend atual publica os booleanos; os diagnósticos adicionais ficam também
disponíveis na interface local. Eventos de catálogo/pessoas e o eco dos estados
de dispositivo são ignorados, sem gerar respostas ou logs de erro.

Os logs de conexão ficam no terminal e em `recordings/session-websocket.log`
(relativo ao `output_root` configurado), com rotação de 256 KiB e uma cópia
`.previous`. Um fechamento informa código e motivo. `1008` com
`Credencial de dispositivo necessaria` indica que o registro não foi autorizado.
Snapshots duplicados não reescrevem o checkpoint no disco.

## Identificação, manifestos e publicação

Preservada a estrutura automática já existente:

```text
recordings/<gravacaoId>/
  <gravacaoId>-rgb000.mp4
  <gravacaoId>-depth000.mp4
  <gravacaoId>.json          # manifesto de publicação existente
  session.json              # inclui sessaoId, participanteId e objeto sessao
  capture/0.json            # manifesto técnico do segmento
  rgb/segments.csv
  depth/segments.csv
  timestamps.csv
  publication/automatic.job.json
  publication/<gravacaoId>.ready
```

`capture/N.json` agora contém `sessaoId`, `gravacaoId`, `participanteId`, arquivos
RGB/depth correspondentes, FPS, resolução, início/fim UTC em milissegundos,
primeiro frame e início monotônico. `session.json` registra o objeto completo da
sessão, inclusive seus participantes/cores, o timestamp de início web e os dados
de sincronização existentes. Esses metadados são locais, não marcadores de
prontidão. As capturas manuais mantêm seus nomes e estrutura anteriores.

O manifesto remoto continua com `v`, `gravacaoId`, `sessaoId`, `iniciadaEm`,
`encerradaEm`, `arquivos[{nome,tipo,tamanhoBytes}]`, sem campos extras que
quebrariam importadores anteriores. Veja o [contrato de publicação](contracts/integracao-brisas-controladas.md)
e seu [schema original](contracts/manifesto-gravacao-v1.schema.json), que restringe
`sessaoId` a UUID. O cliente aceita IDs opacos no WebSocket: para IDs não UUID,
o importador também precisa aceitar esses valores.
Os UUIDs de gravação e nomes de arquivos continuam canônicos.

A publicação só ocorre após `session.json: completed`, exit code zero dos dois
encoders, fsync e validação ffprobe de todos os segmentos. Mídia e manifesto são
enviados com rsync `--delay-updates`; depois, em operação separada, o `.ready`
contendo SHA-256 dos bytes do manifesto. O importador deve exigir esse marcador.
Nunca publicar por presença de `capture/N.json`, nem promover arquivo incompleto
apenas porque ffprobe consegue abri-lo. Nenhum arquivo é apagado automaticamente.

Continuam os limites de quatro horas/10 GB. A captura automática ajusta os
segmentos para até 15 pares + thumbnail dentro das 32 entradas do contrato
existente: com os padrões, 960 segundos. Manual conserva 600 segundos.

## Testes e validação na instalação

```sh
make -j2
bash tests/run_session_integration.sh
bash tests/run_kinect_recorder.sh
bash tests/run_recording_transfer.sh
make -f tests/Makefile TestReleaseABI TestSessionSrt -j2
obj/linux64/Release/tests/lifecycle
python3 tests/validate_session_srt.py
```

Os testes usam POCO/WebSocket reais em loopback, FFmpeg/ffprobe reais, frames
sintéticos e rsync real com destino SSH simulado em diretórios temporários.
Cobrem registro autenticado e sua recusa, framing/ping, JSON inválido, idempotência, início automático,
propriedade SRT, encerramento, falha de encoder/captura, reconexão durante captura,
reinício real do processo, indisponibilidade inicial, backoff após conexões curtas,
conexão ociosa sem telemetria inválida, snapshots nulos, metadados,
publicação interrompida e colisões. O teste SRT usa o transmissor real com PID file
isolado e listener FFmpeg local, sem tocar a transmissão da aplicação em uso.

Para validar com frontend e TouchDesigner:

1. No Mac, inicie o backend acessível na LAN. Configure a URL real e a credencial
   no Linux. Reinicie o openFrameworks e confirme `Registro confirmado` no log.
   Aguarde ao menos 40 segundos: deve permanecer conectado, sem repetir registros.
2. Abra no TouchDesigner um Video Stream In TOP listener em
   `srt://0.0.0.0:9494?mode=listener&latency=20000` (ou a URL exibida na UI).
3. Com o Kinect conectado, inicie uma sessão pelo frontend. Confira o ID na UI,
   a criação de RGB/depth e o estado enviado. O receptor deve mostrar a
   **composição** atual, não necessariamente o RGB cru; os arquivos guardam RGB/depth.
4. Repita o evento de início e envie snapshot com o mesmo ID. A pasta/UUID e a
   conexão SRT devem continuar iguais. Interrompa só o WebSocket, mantendo SRT e
   Kinect, e confira que os arquivos continuam crescendo.
5. Reconecte e envie snapshot ativo. Depois reinicie o aplicativo durante a
   sessão: deve surgir outro UUID associado ao mesmo ID, preservando a pasta anterior.
6. Encerre pelo frontend. Espere a finalização na interface local, confira os MP4 com ffprobe,
   o vínculo no manifesto e a chegada de `.ready` ao Mac. Confirme a importação
   e a exibição pelo frontend. Repita com SRT iniciado manualmente: ele deve continuar.
7. Feche o listener ou desconecte o Kinect. Confira o estado real no backend,
   o diagnóstico na interface local,
   UI responsiva e arquivos preservados. Conexão USB, hardware e importador/rotas
   do backend real precisam dessa validação na instalação; os testes locais não
   comprovam esses componentes externos.
