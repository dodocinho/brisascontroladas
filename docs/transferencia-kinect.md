# Publicação automática das gravações Kinect

> Este guia descreve a publicação **legada das capturas manuais**. Capturas
> associadas ao Corpo Expandido seguem o [contrato v1 com marcador `.ready`](integracao-corpo-expandido.md),
> aguardam toda a captura e usam um UUID por gravação, com múltiplos arquivos.

Cada MP4 RGB e Depth finalizado recebe um UUID v4 próprio e é publicado como
`<uuid>.mp4`, `<uuid>.jpg` e `<uuid>.json`. O JSON final é o único marcador de
publicação para o backend. Os dois vídeos de um mesmo intervalo mantêm
`sessionId`, `segmentIndex` e `stream` (`rgb`/`depth`); não há identificadores de
participantes. Os MP4 locais continuam nos caminhos originais.

## Arquitetura e fechamento

`FonteGlobalKinect::update` continua entregando frames às filas SPSC do
`KinectRecorder`. O worker existente mantém os dois encoders H.264 e o muxer
segmentado. Nenhuma operação de rede ou thumbnail ocorre nessas threads.
`TransmissorSrt` não foi alterado. A contagem de espaço do gravador tolera
sidecars temporários que desaparecem durante a varredura, sem encerrar a captura.

O novo `RecordingTransfer`, pertencente a `GuiApp`, inicia junto da aplicação,
mesmo sem Kinect conectado. Um único worker descobre segmentos, verifica os
containers com ffprobe, prepara os arquivos e transfere as pendências.

O muxer do FFmpeg escreve `segments.csv` **antes** de fechar o arquivo
([segment_end no FFmpeg](https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/segment.c)).
Assim, uma linha CSV, sozinha, não autoriza a transferência. Exigimos:

- Linha CSV completa e evento de captura sem erro.
- Existência do segmento seguinte, aberto somente depois do fechamento anterior;
  ou sessão `completed` com código zero do encoder, persistido **após waitpid**.
- Container H.264 legível, contagem de frames e duração compatíveis com o evento
  de captura e os tempos registrados pelo muxer.

Uma queda do encoder pode deixar os segmentos anteriores utilizáveis. O último
segmento sem confirmação permanece local e não é publicado. Em encerramento
brusco, o worker ignora arquivos ainda em escrita pelos encoders órfãos.

Os eventos `capture/<segmentIndex>.json` persistem os intervalos da captura antes
da finalização. UTC com milissegundos e `steady_clock` são coletados ao iniciar a
captura; as fronteiras dos segmentos usam esse instante mais os offsets da grade
CFR monotônica. Assim, o atraso do encoder não desloca os horários da gravação,
inclusive quando a parada ocorre antes de escoar o buffer inicial. Para parada
solicitada, o instante é coletado em `stop()`. `startedAt`/`endedAt` representam
a linha de tempo da captura no host; não são horários de exposição do sensor. `durationMs` vem
do MP4 validado, enquanto `captureDurationMs` registra o intervalo monotônico
observado. Eles podem diferir por buffering, repetição de frames e arredondamento
CFR. Nenhum desses horários é derivado do momento da transferência.

## Configuração persistente

Edite `bin/data/kinect-recording.json`, mantendo as configurações existentes:

```json
{
  "transfer": {
    "enabled": true,
    "sshHost": "mephistopheles",
    "remoteDirectory": "~/Documents/dev/corpo-expandido-web-front/public/gravacoes_kinect",
    "maxRetries": 10,
    "retryIntervalSeconds": 30,
    "bandwidthKbps": 2048
  },
  "thumbnail": {
    "enabled": true,
    "width": 320,
    "timestampSeconds": 20
  }
}
```

Reinicie o aplicativo para aplicar alterações. `output_root` e `ffmpeg` continuam
sendo utilizados; é necessário `ffprobe` da mesma instalação do FFmpeg (ao lado
do executável configurado, ou no PATH quando `ffmpeg` é apenas um nome).

- `maxRetries`: repetições após a primeira tentativa; `0` = só uma tentativa,
  `-1` = repetir indefinidamente. A contagem é persistente, inclusive após reinício.
- Ao esgotar tentativas, use **Tentar transferências novamente** na interface.
  Isso reabilita também falhas de thumbnail. Vídeos não são apagados.
- `bandwidthKbps`: limite do rsync em **KiB/s**, maior que zero. O padrão limita
  a aproximadamente 2 MiB/s; ajuste à banda livre durante SRT.
- `thumbnail.timestampSeconds`: padrão 20; configure `2` para o exemplo curto.
  Vídeos menores usam um instante imediatamente anterior ao último frame válido,
  evitando arredondamento para além do fim em gravações de poucos frames.
- `transfer.enabled=false`: prepara thumbnail e metadados localmente e conserva
  a fila; não executa SSH/rsync.
- `thumbnail.enabled=false`: publica apenas vídeo e JSON, com `thumbnail:null`.
  Trabalhos já preparados mantêm a escolha original para preservar idempotência.
- O diretório remoto deve ser absoluto ou iniciar por `~/`, usando letras ASCII,
  números, `_`, `-`, `.`, `/`, sem componentes `.`/`..`, espaços ou metacaracteres.
  O host deve ser um alias SSH, não uma linha de comando.

## SSH e Mac

No Mac, habilite **Acesso Remoto**, instale/disponibilize `rsync` e dê permissão de
escrita no diretório ao usuário SSH. No Linux, o alias existente deve equivaler a:

```sshconfig
Host mephistopheles
    HostName IP_OU_NOME_DO_MAC
    User SEU_USUARIO_NO_MAC
    IdentityFile ~/.ssh/id_ed25519
    IdentitiesOnly yes
```

Mantenha chaves e configuração em `~/.ssh`, fora do projeto. Se a chave tem senha,
carregue-a no ssh-agent da sessão que executa o aplicativo. Faça uma primeira
conexão manual e confira a chave do host. Depois valide:

```sh
ssh -oBatchMode=yes -oStrictHostKeyChecking=yes mephistopheles 'command -v rsync'
ssh -oBatchMode=yes mephistopheles 'mkdir -p "$HOME/Documents/dev/corpo-expandido-web-front/public/gravacoes_kinect"'
```

O aplicativo usa `BatchMode=yes`, `StrictHostKeyChecking=yes`, stdin desconectado
e timeouts. Ele não pede senha nem aceita chaves de host automaticamente.

## Protocolo e recuperação

Os arquivos `publication/<nome-original>.job.json` armazenam UUID, preparo,
tentativas, próxima tentativa, último erro e recibo de conclusão. São substituídos
atomicamente com fsync. Um `flock` por `output_root` impede dois workers locais de
transferirem a mesma fila simultaneamente.

1. Envia MP4 e JPEG, em sequência, com rsync e `--partial-dir=.rsync-partial`.
2. Após ambos retornarem sucesso, envia o JSON como `.<uuid>.json.pending`.
3. Executa renomeação no mesmo diretório remoto para `<uuid>.json`.
4. Persiste o recibo local. Se a conexão cair depois da renomeação, a repetição
   usa os mesmos nomes e verifica que o JSON final é idêntico antes de confirmar.
   Um marcador existente diferente bloqueia o envio antes de sobrescrever mídia.

Não se usa `--inplace`. Arquivos temporários do rsync são renomeados no destino;
um JSON final nunca é enviado antes da confirmação dos arquivos de mídia.
UUIDs vêm do gerador criptográfico do kernel; não se reutilizam nomes por sessão.
O backend deve descobrir somente JSONs finais e usar `id` como chave idempotente.

No reinício, sessões e jobs são examinados a partir de `output_root`. Pendências
retomam automaticamente após o intervalo persistido; concluídas são ignoradas.
A ausência de um job é recuperada a partir do CSV e dos eventos de captura. Não
edite/remova jobs concluídos: eles preservam os IDs e recibos. Alterar o destino
não republica automaticamente trabalhos já concluídos.

## Teste manual

1. Valide o SSH acima. Para teste breve, configure `segment_seconds: 5` no JSON
   existente e reinicie o aplicativo.
2. Conecte o Kinect e grave por pelo menos 12 segundos; confirme publicações
   durante a gravação e a publicação do último segmento após **Stop Recording**.
3. Veja os contadores na interface, `transfer.log` e `transfer-process.log` dentro
   de `output_root`, e os arquivos na pasta remota. Para cada JSON final, MP4 e
   JPEG correspondentes devem existir e abrir normalmente.
4. Confira `source`, `device` (hostname real do Linux), os horários UTC,
   `durationMs`, `sizeBytes`, `sessionId`, `stream` e `segmentIndex`.

Para simular perda de rede sem afetar a captura local, desconecte **o Mac** da
rede durante uma transferência. Confira erro e nova tentativa, sem exclusão de
vídeos locais. Feche e reabra o aplicativo ainda sem conexão; os mesmos UUIDs
persistem. Reconecte o Mac: pendências retomam automaticamente. Se o limite já
foi atingido, clique **Tentar transferências novamente**. Verifique que não
aparecem novos IDs para os mesmos segmentos. Para observar o destino durante uma
transferência grande, procure temporários `.rsync-partial`; o JSON final só deve
aparecer após a mídia.

## Testes automatizados

Execute na raiz do projeto:

```sh
bash tests/run_recording_transfer.sh
bash tests/run_kinect_recorder.sh
make -j2
```

O primeiro teste compila o worker sem openFrameworks e usa FFmpeg, ffprobe e rsync
reais. Um SSH simulado executa os comandos remotos num diretório temporário,
sem acessar o Mac. Verifica publicação durante captura, thumbnails curtas,
metadados, reinício sem duplicação, falha de thumbnail, rede indisponível,
limite de tentativas, falha na publicação do JSON, perda da confirmação SSH,
recuperação, desligamento durante processo travado, falha no envio do JPEG,
thumbnail desativada, conflito com marcador remoto existente, gravações menores
que o buffer inicial, remoção concorrente de temporários e rejeição de
configuração insegura. Também rejeita MP4 corrompido, linha CSV incompleta e
último segmento sem confirmação de fechamento. Os artefatos de teste ficam em `/tmp/brisas-transfer-*`.

## Limitações

- Sessões anteriores à inclusão dos eventos de captura não são migradas
  automaticamente: não seria possível afirmar os horários exigidos com precisão.
- A validação inspeciona o container e suas tabelas; não decodifica cada frame do
  vídeo inteiro, evitando competir com a captura. Thumbnail decodifica um frame.
- Não há reparo automático de MP4 incompleto, limpeza de gravações, rotação de
  logs ou remoção de temporários remotos abandonados.
- O worker usa prioridade CPU reduzida, I/O idle, uma thread de thumbnail e
  banda limitada. Isso reduz competição, mas não garante impacto zero em hardware
  ou rede saturados. Meça captura e SRT juntos no equipamento de instalação.
- Eventos e session.json usam substituição atômica; queda de energia antes da
  persistência dos eventos pode deixar o último segmento sem publicação. A fila
  prefere conservar o vídeo a inventar horários ou confirmar arquivo incompleto.
- Linux é o produtor suportado; o destino usa shell POSIX, rsync, cmp e mv,
  disponíveis no ambiente de SSH do Mac. Nomes com espaços no destino não são
  aceitos nesta versão.

## Resultado da validação desta entrega (2026-11-02)

- `make -j2`: passou. Há avisos preexistentes nos addons/UI; o worker e seu teste
  independente também compilaram com `-Wall -Wextra -Werror`.
- `bash tests/run_kinect_recorder.sh`: passou, incluindo decodificação dos vídeos,
  limites de duração/espaço, encoder morto/travado e reinicialização.
- `bash tests/run_recording_transfer.sh`: passou em todos os cenários acima,
  com FFmpeg/ffprobe/rsync reais e SSH simulado. Artefatos desta execução:
  `/tmp/brisas-transfer-h2_h57mg`.
- `make -f tests/Makefile TestReleaseABI -j2` e teste `lifecycle` sem OpenGL:
  passaram.
- A tentativa de SSH real foi recusada antes da conexão: `Bad owner or permissions
  on /etc/ssh/ssh_config.d/20-systemd-ssh-proxy.conf`. Não foi possível validar o
  Mac a partir deste ambiente. A configuração/permissões do SSH precisam estar
  válidas no ambiente de execução; o aplicativo não as altera.
- Kinect físico, GUI/OpenGL e captura com SRT simultâneo não foram exercitados
  nesta entrega. A verificação de desempenho com a instalação real permanece
  necessária.

## Arquivos desta implementação

- `src/RecordingTransfer.h` e `src/RecordingTransfer.cpp`: fila, preparo e protocolo.
- `src/KinectRecorder.cpp`: eventos de captura e tolerância a temporários na
  contagem de espaço.
- `src/GuiApp.h` e `src/GuiApp.cpp`: configuração, ciclo de vida, status e retentativa.
- `bin/data/kinect-recording.json`: valores persistentes de transferência/thumbnail.
- `tests/recording_transfer.cpp`, `tests/validate_recording_transfer.py` e
  `tests/run_recording_transfer.sh`: testes de integração.
- `docs/transferencia-kinect.md`, `docs/gravacao-kinect.md` e `README.md`: documentação.

As alterações locais que já existiam no início do trabalho foram preservadas.
