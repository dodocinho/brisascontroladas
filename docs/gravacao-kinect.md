# Gravação RGB + Depth do Kinect

O painel **Gravação Kinect RGB + Depth**, em Controles Gerais, grava a fonte Kinect em dois vídeos H.264/MP4, 640×480. Ele é independente da captura PNG da composição e da transmissão SRT. Os vídeos de demonstração não são usados para gravar profundidade.

## Usar

1. Instale/disponibilize FFmpeg com `libx264` no `PATH`. Confira com `ffmpeg -hide_banner -encoders | rg libx264`. `ffprobe` é necessário apenas para validar os arquivos.
2. Ligue o Kinect no painel Kinect. Ajuste `depthClippingNear` e `depthClippingFar` no painel Kinect. Ajuste FPS e bitrates no painel de gravação, antes de iniciar.
3. Clique **Start Recording Kinect**. O painel informa preparação, gravação, finalização, tempo de vídeo, GB utilizados, descartes/repetições e erros.
4. Clique **Stop Recording Kinect** e aguarde “Gravação finalizada”. O pedido retorna imediatamente; a finalização dos MP4 acontece em segundo plano. Ao fechar a aplicação, ela aguarda a finalização, com timeout para processos defeituosos.

O arquivo `bin/data/kinect-recording.json` define os valores iniciais. Alterações na interface valem para a próxima sessão e não são persistidas nesse arquivo. O JSON aceita `output_root` (relativo a `bin/data` ou absoluto), `ffmpeg` (executável), `fps` (1–30), `rgb_kbps`, `depth_kbps`, `preset`, `segment_seconds` (1–600) e `queue_frames` (2–60). Configuração inválida gera uma mensagem, sem iniciar uma sessão de vídeo. A gravação manual e a automática copiam a faixa atual dos sliders do Kinect ao iniciar. A escala fica fixa até o fim da gravação e é registrada em `session.json`; alterações nos sliders durante a captura valem para a próxima gravação. As antigas chaves `depth_near_mm` e `depth_far_mm` são ignoradas pelo aplicativo.

Padrões: 30 FPS, 2500 kb/s RGB, 1500 kb/s Depth, `veryfast`, faixa inicial do Kinect 500–2000 mm, segmentos de 600 s e fila de 8 imagens por canal. Limites da aplicação: 4 horas, 10.000.000.000 bytes e reserva de 128.000.000 bytes para finalização. Não há áudio.

```text
bin/data/recordings/
  YYYY-MM-DD_HH-MM-SS/       # UTC; sufixo numérico se houver colisão
    session.json
    timestamps.csv
    rgb-ffmpeg.log
    depth-ffmpeg.log
    rgb/
      segments.csv
      rgb_000.mp4
      rgb_001.mp4
    depth/
      segments.csv
      depth_000.mp4
      depth_001.mp4
```

## Profundidade e sincronização

Uma faixa menor distribui os níveis de cinza por um intervalo menor de distâncias, aumentando a distinção entre profundidades dentro dele. O vídeo continua sendo de 8 bits e com compressão H.264; profundidades fora da faixa são pretas.

A fonte chama `ofxKinect::setRegistration(true)`. O `threadedFunction()` do addon instalado seleciona `FREENECT_DEPTH_REGISTERED`; sem registro ele selecionaria `FREENECT_DEPTH_MM`. Portanto, **`getRawDepthPixels()` contém milímetros de 16 bits**, apesar do comentário antigo “raw 11 bit values” no cabeçalho. `getDepthPixels()` já é uma visualização de 8 bits dependente do clipping; não é usado como entrada do gravador.

O gravador recebe uma cópia do Depth em milímetros e converte na thread de gravação:

- `0` e profundidades fora da faixa configurada viram preto (`0`).
- Dentro da faixa: `1 + floor(254 × (longe − mm) / (longe − perto))`.
- O limite próximo vira `255`; o distante vira `1`.

A quantização é de 8 bits e H.264 é lossy: valores e contornos podem variar após a decodificação. Preto é o tratamento consistente na entrada do encoder, não uma máscara binária exata garantida no arquivo. `session.json` registra `quantized_bits: 8` e `lossless: false`. O TODO de expansão para 16 bits/FFV1 está em `KinectRecorder::quantizeDepth`; essa modalidade não está implementada.

A captura existente continua na thread de `ofxKinect`; os callbacks não são modificados. `FonteGlobalKinect::update()` entrega cada imagem nova a uma fila SPSC pré-alocada. O produtor só copia o buffer e publica o slot, sem esperar por locks, FFmpeg ou disco. Fila cheia descarta a imagem recebida e incrementa o contador. O consumidor move os buffers sem outra cópia integral e faz a conversão de Depth.

O addon descarta os timestamps de hardware e pode sobrescrever imagens antes do `update()`. Os timestamps registrados são do relógio monotônico do host, imediatamente após `ofxKinect::update()`. RGB e Depth novos na mesma atualização recebem o mesmo instante. **Não há garantia de sincronização de exposição, e perdas anteriores à entrega pelo addon são desconhecidas.** A resolução temporal de recebimento depende da frequência de atualização da aplicação.

Os vídeos usam uma grade CFR comum: frame `n` corresponde a `n / FPS` segundos desde o início. Para cada ponto dessa grade, o gravador escolhe a imagem mais recente recebida até aquele instante. Repete a anterior em caso de ausência, ou usa preto antes da primeira imagem. Um atraso de processamento de 100 ms permite consumir as filas. Pausas na captura não encurtam os vídeos; Stop preserva a duração solicitada com arredondamento de até um frame. Um atraso de codificação acima de 5 s encerra a sessão com erro, mantendo os pares já escritos e registrando a duração efetivamente produzida.

Cada linha de `timestamps.csv` corresponde a um par de frames completamente enviado aos encoders:

| Campo | Significado |
| --- | --- |
| `frame`, `pts_ns` | Índice global e tempo de reprodução comum, em nanossegundos |
| `segment`, `segment_frame` | Segmento e índice local para reprodução |
| `rgb_id`, `depth_id` | Sequência observada na aplicação; zero indica ausência inicial |
| `*_received_ns` | Instante de recebimento relativo à origem monotônica; `-1` se ausente |
| `rgb_minus_depth_ns` | Diferença entre os recebimentos escolhidos; só significativa quando ambos existem |
| `*_repeat` | A imagem anterior foi reutilizada |
| `*_missing` | Ainda não chegou nenhuma imagem desse canal |
| `*_queue_dropped` | Total acumulado de entradas rejeitadas pela fila ou por tamanho inválido |
| `*_resampled_away` | Total acumulado de imagens consumidas sem uso por haver uma mais recente no mesmo intervalo |

“Descartados” na interface soma rejeições da fila e imagens não selecionadas na amostragem. “Repetidos” é separado: uma repetição não prova que o sensor perdeu um frame. Imagens pendentes depois do último PTS não pertencem à duração gravada. O CSV e os contadores não medem perdas internas do driver.

## Segmentos, espaço e falhas

São dois processos FFmpeg contínuos. O segment muxer corta cada canal nos mesmos múltiplos de 600 s; keyframes forçados, GOP fechado, sem B-frames e timestamps reiniciados permitem abrir cada MP4 isoladamente. O encoder não reinicia na transição. `segments.csv` de cada canal contém os intervalos produzidos pelo muxer. A configuração segue a [documentação do segment muxer do FFmpeg](https://ffmpeg.org/ffmpeg-formats.html#segment_002c-stream_005fsegment_002c-ssegment).

O orçamento nominal é `(2,5 + 1,5) Mb/s × 14.400 s / 8 = 7,2 GB`. A validação inclui 5% de margem e até 256 bytes por linha de CSV: aproximadamente **7,67 GB**, mais a reserva de 128 MB. Os encoders usam ABR, `maxrate` igual ao bitrate e VBV de dois segundos; imagens simples podem produzir arquivos menores. Configurações cujo orçamento exceda o limite são rejeitadas.

A thread mede os arquivos e espaço livre a cada segundo. Para ao atingir 10 GB menos a reserva, quando o disco tem apenas a reserva livre, ao completar quatro horas, ao desconectar o Kinect ou ao fechar a aplicação. O contador de GB pode ter atraso de um segundo e inclui metadados e logs. O limite protege esta sessão; outros programas podem consumir espaço simultaneamente.

Os pipes são não bloqueantes e atendidos juntos por `poll`, com timeout de cinco segundos por par. `SIGPIPE` é bloqueado apenas na thread do gravador. Saída antecipada, falha de escrita, falta de encoder ou erro de disco encerram a sessão e aparecem na interface/metadados. Stop fecha ambos os pipes e aguarda EOF/finalização; só após timeout usa SIGTERM e, por último, SIGKILL. Os logs ficam na própria sessão.

Um processo morto abruptamente, falta de energia ou encerramento forçado pode perder o **segmento em andamento**. Segmentos já finalizados continuam independentes. Em erro, confira os logs, os códigos de saída e `state` em `session.json`; o último segmento pode conter um frame a mais num canal ou estar incompleto. `frames_written_pairs` contabiliza envio completo aos pipes, não confirmação de persistência de cada frame pelo encoder. O arquivo de metadados é atualizado por substituição atômica a cada segundo e no encerramento.

## TouchDesigner e validação de dez minutos

O [Movie File In TOP suporta MP4 e decodificação H.264](https://derivative.ca/UserGuide/Movie_File_In_TOP). Abra `rgb_NNN.mp4` e `depth_NNN.mp4` de mesmo número em dois TOPs. Use **Specify Index**, unidades de frames e **a mesma expressão/índice** em ambos, com FPS igual ao gravado. Ao mudar o segmento, troque ambos os arquivos e reinicie o índice comum. Para uma linha do tempo global: `segmento = floor(frame / (600 × FPS))`, `índice local = frame % (600 × FPS)`. Evite dois controles de reprodução livres para uma sincronização determinística.

1. Compile com `make -j2`; inicie a aplicação, conecte o Kinect e deixe os padrões do gravador.
2. Grave **10 minutos e 5 segundos**, movimentando uma pessoa/objeto no início e perto do corte de dez minutos. Verifique render, visualização e SRT durante a gravação.
3. Pare e aguarde a finalização. Deve haver dois arquivos por canal: aproximadamente 600 s e 5 s. O primeiro segmento deve ser reproduzível ainda durante a gravação do segundo. A projeção nominal para dez minutos é 300 MB de vídeo; o tamanho real pode ser menor.
4. Execute na raiz do projeto:

   ```sh
   python3 tests/validate_kinect_recording.py bin/data/recordings/PASTA_DA_SESSAO
   ```

   O validador verifica codec, resolução, FPS, PTS, keyframes iniciais, número de frames dos dois canais e duração/contagem por segmento; decodifica todos os MP4 isoladamente e confere o orçamento de disco.
5. Abra os dois pares no TouchDesigner, controle o índice em conjunto e confira movimento, alinhamento, silhuetas e a transição entre segmentos. A profundidade próxima deve estar clara; inválidos e fora da faixa, escuros.
6. Examine `timestamps.csv`: a duração não deve diminuir em trechos com repetições. O campo `hardware_drops` dos metadados explica a limitação do addon.

## Testes automatizados sem Kinect

```sh
bash tests/run_kinect_recorder.sh
```

Compila apenas o módulo independente usando C++17, pthread e o `nlohmann/json` já incluído no openFrameworks. Testa mapeamento de Depth, filas saturadas, pausas e perdas de entrada, Stop assíncrono, reinício, segmentos de um segundo, limite de duração, limite de espaço, orçamento inválido, FFmpeg ausente, preset inválido, processo morto e processo travado. O validador compara os canais, decodifica cada segmento e verifica bandas de Depth conhecidas depois da compressão. Os resultados ficam em `/tmp/brisas-recorder-test.*`.

Para executar também uma gravação **sintética** de 605 segundos, com cortes reais de 600 segundos:

```sh
bash tests/run_kinect_recorder.sh /tmp/brisas-kinect-605s 605
```

Os testes sintéticos não validam USB, timestamps de exposição, desempenho simultâneo com todas as camadas/SRT, reprodução no TouchDesigner ou uma sessão real de quatro horas. Essas verificações precisam do ambiente de apresentação.

## Transferência automática

A publicação de segmentos finalizados, thumbnails e metadados para o Mac está
implementada em um worker independente. Consulte [transferencia-kinect.md](transferencia-kinect.md)
para configuração, protocolo de publicação, recuperação e testes.
