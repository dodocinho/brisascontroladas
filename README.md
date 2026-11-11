# Brisas Controladas

projeto revivido 🔥❤️‍🔥🐦❤️‍🔥🔥

## ~é possível controlar uma brisa?

Uma controladora para misturar imagens de webcams, Kinect, vídeos e fotos. Cada **brisa** é uma camada visual: você pode combinar várias, mudar a ordem, ajustar a transparência e aplicar efeitos com shaders ou animações feitas no openFrameworks.

![Screenshot da controladora](https://github.com/evandrododo/brisascontroladas/raw/main/bin/data/img/screenshot.png "Screenshot da controladora")

## O que mudou por aqui

### Uma câmera, várias brisas

Agora todas as brisas que usam o Kinect recebem imagem da mesma fonte, a `FonteGlobalKinect`. Ela cuida do aparelho e disponibiliza os frames de cor (RGB) e profundidade para as camadas.

Essa fonte é um **singleton**: existe uma única instância dela no programa. A cada atualização da interface, ela lê o Kinect antes de atualizar as brisas. Assim, as camadas trabalham com os mesmos frames disponíveis.

- `Colunas` e `KinectContornosBrisa` pegam a profundidade diretamente dessa fonte.
- `SombraBrisa`, `MatrizBrisa` e `KinectCamBrisa` passam pelo `ProcessamentoKinect`, que prepara a imagem para cada efeito.

### A imagem é compartilhada; os ajustes são de cada camada

O `ProcessamentoKinect` reúne filtros como blur, espelhamento, brilho, contraste e rastro. Cada camada que usa esse processamento tem suas próprias configurações e sua própria cópia da imagem para trabalhar.

Dá para ter, por exemplo, uma sombra bem desfocada e outra espelhada usando a mesma câmera. Mexer no filtro de uma não altera a imagem recebida pela outra.

Os arquivos da fonte e do processamento ficam em `src/Brisas/kinect/`. A antiga `FonteKinect`, que misturava captura e efeitos, foi substituída por essa divisão.

### Criar e apagar camadas ficou mais seguro

A `ColecaoBrisas` agora cuida da vida das camadas usando `unique_ptr`. O nome parece complicado, mas a ideia é simples: **cada camada tem um dono, e esse dono libera seus recursos quando ela é removida**.

Também mudou o jeito de identificar as brisas. Cada uma recebe um ID que continua o mesmo ao mudar de posição. Isso mantém o foco e a escolha de imagem dos shaders ligados à camada certa. Se a camada usada por um shader for excluída, a seleção é limpa.

A exclusão acontece depois que a interface termina de desenhar. Ao fechar o programa, imagens, filtros e recursos gráficos são liberados antes das janelas.

### Sem Kinect conectado? A interface continua funcionando

`Colunas` não fica mais presa esperando a câmera conectar. As brisas verificam se há imagem antes de tentar processá-la.

Sem o aparelho, a fonte pode usar estes vídeos de demonstração, se estiverem disponíveis:

- `bin/data/kinectdemo/depthdance.mp4`
- `bin/data/kinectdemo/rgbdance.mp4`

Esses vídeos são opcionais. Se um deles não existir ou não carregar, o canal correspondente fica sem imagem e as camadas que dependem dele limpam a saída.

## Quer entender o código?

Tem um [guia de arquitetura em `docs/arquitetura.md`](docs/arquitetura.md). Ele mostra quem cuida de cada recurso, o caminho da imagem até a projeção e por que a ordem de criação e encerramento importa.

É um bom ponto de partida para criar uma nova brisa ou mexer nos filtros sem precisar descobrir tudo lendo os arquivos de uma vez.

## Compilar e conferir

No ambiente Linux com openFrameworks e os addons do projeto configurados, execute na pasta do projeto:

```sh
make -j2
make RunRelease
```

Para compilar e executar os testes de gerenciamento das camadas:

```sh
make -f tests/Makefile TestReleaseABI -j2
obj/linux64/Release/tests/lifecycle
```

Os testes gráficos precisam de uma sessão OpenGL disponível:

```sh
obj/linux64/Release/tests/lifecycle --gl
```

A compilação e os testes de criação/exclusão, filtros, compartilhamento de frames e encerramento gráfico passaram nesta reorganização. A conexão USB e os controles físicos ainda precisam ser conferidos com um Kinect. O [guia de arquitetura](docs/arquitetura.md#validação) explica também como testar com vídeos de demonstração.

## Gravar RGB + Depth do Kinect

O painel **Gravação Kinect RGB + Depth** grava os dois canais em H.264/MP4,
640×480, até 30 FPS, com segmentos de dez minutos e limite de 10 GB/quatro horas.
A gravação usa filas limitadas e FFmpeg em segundo plano. O Depth é uma
visualização quantizada em 8 bits; não preserva os dados originais sem perdas.

Configuração inicial: `bin/data/kinect-recording.json`. Veja o
[guia de gravação](docs/gravacao-kinect.md) para uso, sincronização no TouchDesigner,
limitações e validação de uma sessão de dez minutos.

Teste independente, sem Kinect ou janela gráfica:

```sh
bash tests/run_kinect_recorder.sh
```

A transferência automática de gravações Kinect para o Mac, com thumbnails,
metadados e recuperação de falhas, está documentada em
[docs/transferencia-kinect.md](docs/transferencia-kinect.md).

## Integração com Corpo Expandido

Controle automático de gravação e SRT por eventos WebSocket POCO, registro e
estados para o backend, vínculo explícito de sessão e publicação rsync confirmada
por `.ready`, preservando os controles manuais. Veja [configuração, arquitetura, recuperação e testes](docs/integracao-corpo-expandido.md).
O exemplo vem desativado até configurar a URL `/api/v1/eventos`, o destino do Mac
e a credencial `GRAVADOR_TOKEN` do backend. No Linux, use `tokenFile` ou
`tokenEnvironment`; `CORPO_EXPANDIDO_TOKEN` é o nome da variável, não o segredo.
