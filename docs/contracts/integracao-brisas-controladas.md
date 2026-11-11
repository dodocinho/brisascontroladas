# Corpo Expandido ↔ Brisas Controladas — publicação v1

O cliente openFrameworks usa o canal atual `/api/v1/eventos` com
`Authorization: Bearer <GRAVADOR_TOKEN>` e registro `cliente.registrar`.
A configuração, as mensagens `tipo`/`versao`, a confirmação do registro e os
controles manuais estão no [guia de integração](../integracao-corpo-expandido.md).
O antigo canal `/api/v1/gravador/ws` e o envelope `v`/`type` não são usados pelo cliente.

Perder o WebSocket não encerra a gravação. A reconexão concilia o snapshot com
a captura em andamento; reiniciar o processo cria outro UUID, preservando os
arquivos anteriores. O formato de publicação abaixo permanece independente do
formato dos eventos WebSocket.

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
- `sessaoId`: ID recebido do backend, copiado literalmente. O schema original e o importador que o aplica exigem UUID; o cliente WebSocket aceita IDs opacos. Usar outro formato exige atualizar o importador e seu schema em conjunto.
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

## Validação na instalação

O catálogo do backend deve exigir o manifesto, o marcador correspondente e os
arquivos declarados antes de disponibilizar a gravação. A presença de um MP4 ou
a estabilidade de seu tamanho não confirma o término da captura.

Verifique no frontend a gravação associada ao `sessaoId` correto depois da
transferência. A recepção SRT no TouchDesigner é independente da publicação de
arquivos; confira ambos os caminhos no [roteiro de validação](../integracao-corpo-expandido.md#testes-e-validação-na-instalação).
A gravação e a transferência manuais permanecem disponíveis conforme o
[guia de transferência](../transferencia-kinect.md).
