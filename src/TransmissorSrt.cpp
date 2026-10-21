#include "TransmissorSrt.h"

#include "ofLog.h"
#include "ofUtils.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <iterator>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char **environ;

namespace
{

const char *caminhoPid()
{
    return "/tmp/brisascontroladas-srt.pid";
}

void fecharQuiet(int fd)
{
    if (fd >= 0) close(fd);
}

bool processoEhFfmpeg(int numero)
{
    std::ifstream entrada("/proc/" + std::to_string(numero) + "/cmdline", std::ios::binary);
    if (!entrada) return false;
    const std::string dados((std::istreambuf_iterator<char>(entrada)), std::istreambuf_iterator<char>());
    return dados.find("ffmpeg") != std::string::npos;
}

// Um fechamento brusco pode deixar o ffmpeg segurando a porta. Na próxima
// transmissão a gente só encerra esse processo, conferindo o comando antes.
void encerrarOrfao()
{
    std::ifstream entrada(caminhoPid());
    int antigo = 0;
    if (!(entrada >> antigo) || antigo <= 1) return;
    if (!processoEhFfmpeg(antigo)) return;

    kill(antigo, SIGTERM);
    for (int i = 0; i < 10; ++i)
    {
        if (kill(antigo, 0) != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (kill(antigo, 0) == 0) kill(antigo, SIGKILL);
    std::remove(caminhoPid());
}

void gravarPid(int numero)
{
    std::ofstream saida(caminhoPid());
    if (saida) saida << numero;
}

void removerPid()
{
    std::remove(caminhoPid());
}

bool escreveTudo(int fd, const unsigned char *dados, std::size_t total)
{
    while (total > 0)
    {
        const ssize_t escrito = ::write(fd, dados, total);
        if (escrito < 0)
        {
            if (errno == EINTR) continue;
            return false;
        }
        if (escrito == 0) return false;
        dados += escrito;
        total -= static_cast<std::size_t>(escrito);
    }
    return true;
}

struct EnderecoLocal
{
    std::string interfaceNome;
    std::string ip;
};

bool interfaceVirtual(const std::string &nome)
{
    if (nome == "lo") return true;
    const char *prefixos[] = {"docker", "veth", "virbr", "br-", "tun", "tap", "tailscale"};
    for (const char *prefixo : prefixos)
    {
        if (nome.rfind(prefixo, 0) == 0) return true;
    }
    return false;
}

int prioridadeInterface(const std::string &nome)
{
    if (nome.rfind("en", 0) == 0 || nome.rfind("eth", 0) == 0 || nome.rfind("wl", 0) == 0) return 0;
    return 1;
}

std::vector<EnderecoLocal> listarEnderecos()
{
    std::vector<EnderecoLocal> lista;
    ifaddrs *interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return lista;

    for (ifaddrs *item = interfaces; item != nullptr; item = item->ifa_next)
    {
        if (item->ifa_addr == nullptr || item->ifa_addr->sa_family != AF_INET) continue;
        const std::string nome = item->ifa_name != nullptr ? item->ifa_name : "";
        if (interfaceVirtual(nome)) continue;

        char texto[INET_ADDRSTRLEN];
        const auto *endereco = reinterpret_cast<sockaddr_in *>(item->ifa_addr);
        if (inet_ntop(AF_INET, &endereco->sin_addr, texto, sizeof(texto)) == nullptr) continue;
        const std::string ip = texto;
        if (ip == "127.0.0.1" || ip == "0.0.0.0") continue;
        lista.push_back({nome, ip});
    }
    freeifaddrs(interfaces);

    std::stable_sort(lista.begin(), lista.end(), [](const EnderecoLocal &a, const EnderecoLocal &b) {
        return prioridadeInterface(a.interfaceNome) < prioridadeInterface(b.interfaceNome);
    });
    return lista;
}

std::string limpaIp(std::string ip)
{
    ip.erase(std::remove_if(ip.begin(), ip.end(), [](unsigned char c) { return std::isspace(c); }), ip.end());
    return ip;
}

bool ipDeMac(const std::string &ip)
{
    if (ip.empty() || ip.size() > 15) return false;
    int numeros = 0;
    int pontos = 0;
    int grupo = -1;
    for (char c : ip)
    {
        if (c == '.')
        {
            if (grupo < 0) return false;
            ++pontos;
            grupo = -1;
            continue;
        }
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        if (grupo < 0)
        {
            grupo = c - '0';
            ++numeros;
        }
        else
        {
            grupo = grupo * 10 + (c - '0');
            if (grupo > 255) return false;
        }
    }
    return pontos == 3 && numeros == 4 && grupo >= 0;
}

} // namespace

TransmissorSrt::~TransmissorSrt()
{
    pedidoLigado = false;
    encerrarProcesso();
}

namespace
{
int limitaLatencia(int latenciaMs)
{
    if (latenciaMs < 5) return 5;
    if (latenciaMs > 200) return 200;
    return latenciaMs;
}
}

void TransmissorSrt::solicitarInicio(const std::string &ipMac, int porta, int latenciaMs)
{
    if (porta < 1) porta = 1;
    if (porta > 65535) porta = 65535;
    const std::string ip = limpaIp(ipMac);
    if (!ipDeMac(ip))
    {
        falhaImediata("IP do Mac inválido. Use quatro números, como 192.168.1.4.");
        return;
    }

    ipPedida = ip;
    portaPedida = porta;
    latenciaPedidaMs = limitaLatencia(latenciaMs);
    tentativas = 0;
    proximaTentativa = 0;
    houveErro = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        mensagemParada.clear();
        ultimaFalha.clear();
        historico.clear();
        mensagem = "Ligando para o Mac...";
    }
    pedidoLigado = true;
}

void TransmissorSrt::ajustarLatencia(int latenciaMs)
{
    latenciaPedidaMs = limitaLatencia(latenciaMs);
}

void TransmissorSrt::solicitarParada()
{
    const bool estava = pedidoLigado;
    pedidoLigado = false;
    houveErro = false;
    encerrarProcesso();
    if (estava)
    {
        std::lock_guard<std::mutex> lock(mutex);
        mensagemParada = "Transmissão encerrada.";
        mensagem.clear();
    }
}

void TransmissorSrt::parar()
{
    solicitarParada();
}

void TransmissorSrt::falhaImediata(const std::string &texto)
{
    pedidoLigado = false;
    houveErro = true;
    {
        std::lock_guard<std::mutex> lock(mutex);
        mensagemParada = texto;
        mensagem.clear();
    }
    ofLogError("TransmissorSrt") << texto;
}

void TransmissorSrt::atualizar(int largura, int altura)
{
    if (!pedidoLigado)
    {
        if (ffmpegNoAr || escritor.joinable() || leitor.joinable()) encerrarProcesso();
        return;
    }

    if (largura < 0) largura = 0;
    if (altura < 0) altura = 0;
    largura &= ~1;
    altura &= ~1;

    if (ffmpegNoAr)
    {
        const bool vivo = aindaVivo();
        const bool configMudou = ipPedida != ipAtiva || portaPedida != portaAtiva ||
                                 latenciaPedidaMs != latenciaAtivaMs;
        const bool tamanhoMudou = largura >= 2 && altura >= 2 &&
                                  (largura != larguraAtiva || altura != alturaAtiva);
        if (vivo && !tamanhoMudou && !configMudou) return;

        encerrarProcesso();
        if (vivo && tamanhoMudou)
        {
            proximaTentativa = 0;
            std::lock_guard<std::mutex> lock(mutex);
            mensagem = "A projeção mudou de tamanho. Ligando de novo...";
        }
        else if (vivo && configMudou)
        {
            proximaTentativa = 0;
            std::lock_guard<std::mutex> lock(mutex);
            mensagem = "Ligando de novo com o IP ou a porta novos...";
        }
    }

    if (largura < 2 || altura < 2)
    {
        std::lock_guard<std::mutex> lock(mutex);
        mensagem = "A janela de projeção está pequena demais para transmitir.";
        return;
    }

    const float agora = ofGetElapsedTimef();
    if (agora < proximaTentativa)
    {
        std::lock_guard<std::mutex> lock(mutex);
        const int falta = std::max(1, static_cast<int>(proximaTentativa - agora + 0.999f));
        mensagem = "Tentativa " + std::to_string(tentativas) +
                   " sem resposta do Mac. Nova ligação em " + std::to_string(falta) + " s.";
        if (!ultimaFalha.empty()) mensagem += "\n" + ultimaFalha;
        return;
    }

    abrirProcesso(largura, altura);
}

bool TransmissorSrt::aceitaQuadro() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return pedidoLigado && aceitandoEscrita && !ocupado && !temQuadro;
}

void TransmissorSrt::enviar(const ofPixels &quadro)
{
    if (!quadro.isAllocated() || quadro.getNumChannels() != 4) return;
    if (quadro.getWidth() != static_cast<std::size_t>(larguraAtiva) ||
        quadro.getHeight() != static_cast<std::size_t>(alturaAtiva))
    {
        return;
    }

    ofPixels copia = quadro;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!aceitandoEscrita || ocupado || temQuadro) return;
        quadroPendente = std::move(copia);
        temQuadro = true;
    }
    esperaQuadro.notify_one();
}

FaseSrt TransmissorSrt::fase() const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (pedidoLigado && ffmpegNoAr && quadrosEscritos > 0) return FaseSrt::Transmitindo;
    if (pedidoLigado) return FaseSrt::Aguardando;
    if (houveErro) return FaseSrt::Erro;
    return FaseSrt::Desligada;
}

std::string TransmissorSrt::status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!pedidoLigado)
    {
        if (!mensagemParada.empty()) return mensagemParada;
        return "Desligada.";
    }
    if (quadrosEscritos > 0 && ffmpegNoAr)
    {
        std::string texto = "Conectado. Enviando " + std::to_string(larguraAtiva) + "x" + std::to_string(alturaAtiva);
        if (reduzida) texto += ", limitada a 1080p";
        texto += " | " + std::to_string(quadrosEscritos) + " quadros no ffmpeg";
        return texto;
    }
    if (!mensagem.empty()) return mensagem;
    return "Preparando a ligação...";
}

std::string TransmissorSrt::logRecente() const
{
    std::lock_guard<std::mutex> lock(mutex);
    if (historico.empty()) return "(o ffmpeg ainda não escreveu nada)";
    std::string texto;
    for (const std::string &linha : historico)
    {
        if (!texto.empty()) texto += "\n";
        texto += linha;
    }
    return texto;
}

std::string TransmissorSrt::ultimaMensagem() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return ultimaFalha;
}

std::string TransmissorSrt::comando() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return comandoAtivo;
}

int TransmissorSrt::quadrosEnviados() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return quadrosEscritos;
}

std::string TransmissorSrt::urlTouchDesigner(int porta, int latenciaMs) const
{
    return "srt://0.0.0.0:" + std::to_string(porta) + "?mode=listener&latency=" +
           std::to_string(limitaLatencia(latenciaMs) * 1000);
}

std::string TransmissorSrt::urlDesteLinux(const std::string &ipMac, int porta, int latenciaMs) const
{
    return "srt://" + ipMac + ":" + std::to_string(porta) +
           "?mode=caller&pkt_size=1316&transtype=live&latency=" +
           std::to_string(limitaLatencia(latenciaMs) * 1000);
}

std::string TransmissorSrt::enderecoDesteLinux() const
{
    const auto lista = listarEnderecos();
    if (lista.empty()) return "";
    return lista.front().ip + " (" + lista.front().interfaceNome + ")";
}

bool TransmissorSrt::aindaVivo()
{
    if (pid <= 0) return false;
    int statusProcesso = 0;
    const pid_t resultado = waitpid(pid, &statusProcesso, WNOHANG);
    if (resultado == 0) return true;
    pid = -1;
    return false;
}

void TransmissorSrt::registraLinha(const std::string &linha)
{
    if (linha.empty()) return;
    std::lock_guard<std::mutex> lock(mutex);
    historico.push_back(linha);
    if (historico.size() > 8) historico.erase(historico.begin());
    if (linha.find("failed") != std::string::npos ||
        linha.find("Error") != std::string::npos ||
        linha.find("error") != std::string::npos ||
        linha.find("Invalid") != std::string::npos)
    {
        ultimaFalha = linha;
    }
}

void TransmissorSrt::loopStderr()
{
    const int fd = fdDoLeitor;
    std::string resto;
    char buffer[512];
    while (true)
    {
        const ssize_t lido = ::read(fd, buffer, sizeof(buffer));
        if (lido < 0)
        {
            if (errno == EINTR) continue;
            break;
        }
        if (lido == 0)
        {
            if (!resto.empty()) registraLinha(resto);
            break;
        }
        resto.append(buffer, buffer + lido);
        std::size_t quebra = resto.find_first_of("\r\n");
        while (quebra != std::string::npos)
        {
            registraLinha(resto.substr(0, quebra));
            resto.erase(0, quebra + 1);
            quebra = resto.find_first_of("\r\n");
        }
        if (resto.size() > 8192) resto.erase(0, resto.size() - 4096);
    }
}

void TransmissorSrt::loopEscrita()
{
    // Sem isso, o fechamento do pipe mata o programa inteiro com SIGPIPE.
    sigset_t bloqueados;
    sigemptyset(&bloqueados);
    sigaddset(&bloqueados, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &bloqueados, nullptr);

    const int fd = fdDoEscritor;
    while (true)
    {
        ofPixels local;
        {
            std::unique_lock<std::mutex> lock(mutex);
            esperaQuadro.wait(lock, [&] { return temQuadro || !aceitandoEscrita; });
            if (!aceitandoEscrita) break;
            local = std::move(quadroPendente);
            temQuadro = false;
            ocupado = true;
        }

        const bool ok = local.isAllocated() &&
                        escreveTudo(fd, local.getData(), local.getTotalBytes());
        {
            std::lock_guard<std::mutex> lock(mutex);
            ocupado = false;
            if (ok) ++quadrosEscritos;
        }
        if (!ok) break;
    }
}

void TransmissorSrt::encerrarProcesso()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        aceitandoEscrita = false;
    }
    esperaQuadro.notify_all();

    if (stdinFd >= 0)
    {
        close(stdinFd);
        stdinFd = -1;
    }
    if (pid > 0) kill(pid, SIGTERM);
    if (escritor.joinable()) escritor.join();

    if (pid > 0)
    {
        for (int i = 0; i < 20; ++i)
        {
            int statusProcesso = 0;
            const pid_t resultado = waitpid(pid, &statusProcesso, WNOHANG);
            if (resultado != 0)
            {
                pid = -1;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (pid > 0)
        {
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
            pid = -1;
        }
    }

    if (leitor.joinable()) leitor.join();
    if (stderrFd >= 0)
    {
        close(stderrFd);
        stderrFd = -1;
    }

    ffmpegNoAr = false;
    fdDoEscritor = -1;
    fdDoLeitor = -1;
    {
        std::lock_guard<std::mutex> lock(mutex);
        ocupado = false;
        temQuadro = false;
        quadrosEscritos = 0;
        quadroPendente.clear();
    }
    removerPid();
}

void TransmissorSrt::abrirProcesso(int largura, int altura)
{
    if (escritor.joinable() || leitor.joinable() || ffmpegNoAr) encerrarProcesso();
    encerrarOrfao();

    int entrada[2] = {-1, -1};
    int erros[2] = {-1, -1};
    if (pipe(entrada) != 0 || pipe(erros) != 0)
    {
        fecharQuiet(entrada[0]);
        fecharQuiet(entrada[1]);
        fecharQuiet(erros[0]);
        fecharQuiet(erros[1]);
        falhaImediata("Não consegui abrir o canal de vídeo para o ffmpeg.");
        return;
    }

    ipAtiva = ipPedida;
    portaAtiva = portaPedida;
    latenciaAtivaMs = latenciaPedidaMs;
    larguraAtiva = largura;
    alturaAtiva = altura;
    reduzida = false;
    proximaTentativa = ofGetElapsedTimef() + 1.f;
    ++tentativas;

    // Caller, MPEG-TS, buffer curto. O max_delay padrão do mpegts é 0,7 s
    // e era a maior parte da demora de um segundo.
    const std::string destino = urlDesteLinux(ipPedida, portaPedida, latenciaPedidaMs);

    std::vector<std::string> args = {
        "ffmpeg",
        "-hide_banner",
        "-loglevel", "warning",
        "-fflags", "nobuffer",
        "-flags", "low_delay",
        "-use_wallclock_as_timestamps", "1",
        "-f", "rawvideo",
        "-pixel_format", "bgra",
        "-video_size", std::to_string(largura) + "x" + std::to_string(altura),
        "-framerate", "30",
        "-i", "pipe:0",
        "-an",
        "-c:v", "libx264",
        "-preset", "ultrafast",
        "-tune", "zerolatency",
        "-bf", "0",
        "-g", "8",
        "-x264-params", "rc-lookahead=0:sync-lookahead=0:sliced-threads=1:bframes=0",
        "-pix_fmt", "yuv420p",
        "-max_delay", "0",
        "-muxdelay", "0",
        "-muxpreload", "0",
        "-f", "mpegts",
        destino};

    std::vector<char *> argv;
    argv.reserve(args.size() + 1);
    for (std::string &arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t acoes;
    posix_spawn_file_actions_init(&acoes);
    posix_spawn_file_actions_adddup2(&acoes, entrada[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&acoes, erros[1], STDERR_FILENO);
    posix_spawn_file_actions_addopen(&acoes, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addclose(&acoes, entrada[0]);
    posix_spawn_file_actions_addclose(&acoes, entrada[1]);
    posix_spawn_file_actions_addclose(&acoes, erros[0]);
    posix_spawn_file_actions_addclose(&acoes, erros[1]);

    pid_t novo = -1;
    const int rc = posix_spawnp(&novo, "ffmpeg", &acoes, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&acoes);
    fecharQuiet(entrada[0]);
    fecharQuiet(erros[1]);

    if (rc != 0)
    {
        fecharQuiet(entrada[1]);
        fecharQuiet(erros[0]);
        falhaImediata(std::string("Não abri o ffmpeg: ") + std::strerror(rc));
        return;
    }

    stdinFd = entrada[1];
    stderrFd = erros[0];
    fdDoEscritor = stdinFd;
    fdDoLeitor = stderrFd;
    fcntl(stdinFd, F_SETFD, FD_CLOEXEC);
    fcntl(stderrFd, F_SETFD, FD_CLOEXEC);
    pid = static_cast<int>(novo);
    inicioTentativa = ofGetElapsedTimef();
    ffmpegNoAr = true;
    {
        std::lock_guard<std::mutex> lock(mutex);
        aceitandoEscrita = true;
        quadrosEscritos = 0;
        temQuadro = false;
        ocupado = false;
        historico.clear();
        mensagem = "Tentativa " + std::to_string(tentativas) + ": ligando para " + destino;
    }
    {
        std::string visivel;
        for (const std::string &arg : args)
        {
            if (!visivel.empty()) visivel += " ";
            visivel += arg;
        }
        std::lock_guard<std::mutex> lock(mutex);
        comandoAtivo = visivel;
    }
    gravarPid(pid);
    escritor = std::thread(&TransmissorSrt::loopEscrita, this);
    leitor = std::thread(&TransmissorSrt::loopStderr, this);
    ofLogNotice("TransmissorSrt") << "Ligando: " << destino;
}
