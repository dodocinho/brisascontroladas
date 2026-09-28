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
