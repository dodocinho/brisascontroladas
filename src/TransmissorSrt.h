#pragma once

#include "ofPixels.h"
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Igual ao kinect-controller: o Mac escuta (listener) e este Linux liga (caller).
// A imagem enviada é sempre 800x600, menor que a projeção, para caber em tempo real.
enum class FaseSrt
{
    Desligada,
    Aguardando,
    Transmitindo,
    Erro
};

class TransmissorSrt
{
public:
    TransmissorSrt() = default;
    ~TransmissorSrt();
    TransmissorSrt(const TransmissorSrt &) = delete;
    TransmissorSrt &operator=(const TransmissorSrt &) = delete;

    static constexpr int larguraEnvio = 800;
    static constexpr int alturaEnvio = 600;
    static constexpr int latenciaPadraoMs = 20;

    void solicitarInicio(const std::string &ipMac, int porta, int latenciaMs);
    void ajustarLatencia(int latenciaMs);
    void solicitarParada();
    void parar();

    // Roda no desenho da projeção. A largura e a altura precisam ser pares.
    void atualizar(int largura, int altura);
    bool aceitaQuadro() const;
    void enviar(const ofPixels &quadro);

    bool estaLigado() const { return pedidoLigado; }
    bool ffmpegEstaVivo() const { return ffmpegNoAr; }
    FaseSrt fase() const;
    std::string status() const;
    std::string logRecente() const;
    std::string ultimaMensagem() const;
    std::string comando() const;
    int quadrosEnviados() const;
    int numeroTentativas() const { return tentativas; }
    std::string urlTouchDesigner(int porta, int latenciaMs) const;
    std::string urlDesteLinux(const std::string &ipMac, int porta, int latenciaMs) const;
    std::string enderecoDesteLinux() const;
    std::string ipEmUso() const { return ipAtiva; }
    int portaEmUso() const { return portaAtiva; }

private:
    void abrirProcesso(int largura, int altura);
    void encerrarProcesso();
    void falhaImediata(const std::string &texto);
    void loopEscrita();
    void loopStderr();
    void registraLinha(const std::string &linha);
    bool aindaVivo();

    bool pedidoLigado = false;
    bool houveErro = false;
    bool aceitandoEscrita = false;
    bool ffmpegNoAr = false;
    bool ocupado = false;
    bool temQuadro = false;
    bool reduzida = false;
    int tentativas = 0;
    int portaPedida = 9494;
    int portaAtiva = 9494;
    int latenciaPedidaMs = latenciaPadraoMs;
    int latenciaAtivaMs = latenciaPadraoMs;
    int larguraAtiva = 0;
    int alturaAtiva = 0;
    int quadrosEscritos = 0;
    float proximaTentativa = 0;
    std::string ipPedida = "192.168.1.4";
    std::string ipAtiva;
    std::string comandoAtivo;
    std::vector<std::string> historico;
    int stdinFd = -1;
    int stderrFd = -1;
    int fdDoEscritor = -1;
    int fdDoLeitor = -1;
    int pid = -1;
    float inicioTentativa = 0;
    ofPixels quadroPendente;
    std::string mensagem;
    std::string mensagemParada;
    std::string ultimaFalha;
    std::thread escritor;
    std::thread leitor;
    mutable std::mutex mutex;
    std::condition_variable esperaQuadro;
};
