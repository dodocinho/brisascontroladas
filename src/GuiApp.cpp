#include "GuiApp.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>
#include <memory>
using namespace std;

namespace
{
std::string hostDaUrl(const std::string &url)
{
    const auto esquema = url.find("://");
    if (esquema == std::string::npos) return {};
    const auto inicio = esquema + 3;
    auto fim = url.find_first_of(":/", inicio);
    if (fim == std::string::npos) fim = url.size();
    return url.substr(inicio, fim - inicio);
}

std::string substituiHost(const std::string &url, const std::string &host)
{
    const auto esquema = url.find("://");
    if (esquema == std::string::npos) return url;
    const auto inicio = esquema + 3;
    auto fim = url.find_first_of(":/", inicio);
    if (fim == std::string::npos) fim = url.size();
    return url.substr(0, inicio) + host + url.substr(fim);
}

bool ipValido(const std::string &ip)
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

std::string limpaIp(std::string ip)
{
    ip.erase(std::remove_if(ip.begin(), ip.end(), [](unsigned char c) { return std::isspace(static_cast<unsigned char>(c)); }), ip.end());
    return ip;
}
}


void GuiApp::setup()
{
    WindowManager::getInstance().setGuiWindowSize(ofGetWidth(), ofGetHeight());

    janelaRecursos = ofGetMainLoop()->getCurrentWindow();
    gui = std::make_unique<ofxImGui::Gui>();
    gui->setup(nullptr, false);
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowRounding = 5;
    // change the main color of buttons, headers, etc
    // style.Colors[ImGuiCol_Button] = ImVec4(0.4, 0.2, 0.8, 1.0);

    // change the collapsed header style
    style.Colors[ImGuiCol_Header] = ImVec4(0.3, 0.0, 0.5, 1.0);

    btnCriaColunas = gui->loadTexture("../data/img/icon/colunas.png");
    btnCriaVideo = gui->loadTexture("../data/img/icon/video.png");
    btnCriaPoligono = gui->loadTexture("../data/img/icon/poligon.png");
    btnCriaKinect = gui->loadTexture("../data/img/icon/kinect.png");
    btnCriaImg = gui->loadTexture("../data/img/icon/img.png");
    btnCriaGif = gui->loadTexture("../data/img/icon/gif.png");
    btnCriaMatriz = gui->loadTexture("../data/img/icon/matriz.png");
    btnCriaSombras = gui->loadTexture("../data/img/icon/sombras.png");
    btnCriaMandala = gui->loadTexture("../data/img/icon/mandala.png");
    btnCriaBorda = gui->loadTexture("../data/img/icon/borda.png");
    btnCriaKinectCam = gui->loadTexture("../data/img/icon/kinectcam.png");
    btnCriaCamera = gui->loadTexture("../data/img/icon/camera.png");
    btnCriaBody = gui->loadTexture("../data/img/icon/camera.png");
    btnCriaFinder = gui->loadTexture("../data/img/icon/camera.png");

    ImGui::GetIO().MouseDrawCursor = false;
    desenhaMiniaturas = true;

    coresPaleta.push_back(ofColor::fromHex(0x00126D));
    coresPaleta.push_back(ofColor::fromHex(0xC0126D));
    coresPaleta.push_back(ofColor::fromHex(0x000000));

    iBlend = 2;
    anguloKinect = 0;
    depthClippingFar = 2000;
    depthClippingNear = 500;
    recordingConfig.outputRoot = ofToDataPath("recordings", true);
    std::string srtHost;
    try
    {
        const auto path = ofToDataPath("kinect-recording.json", true);
        if (ofFile::doesFileExist(path))
        {
            const auto config = ofLoadJson(path);
            recordingConfig.fps = config.value("fps", recordingConfig.fps);
            recordingConfig.rgbKbps = config.value("rgb_kbps", recordingConfig.rgbKbps);
            recordingConfig.depthKbps = config.value("depth_kbps", recordingConfig.depthKbps);
            recordingConfig.segmentSeconds = config.value("segment_seconds", recordingConfig.segmentSeconds);
            recordingConfig.queueFrames = config.value("queue_frames", recordingConfig.queueFrames);
            recordingConfig.preset = config.value("preset", recordingConfig.preset);
            recordingConfig.ffmpeg = config.value("ffmpeg", recordingConfig.ffmpeg);
            recordingConfig.outputRoot = ofToDataPath(config.value("output_root", std::string("recordings")), true);
            const auto integration = config.value("corpoExpandido", ofJson::object());
            sessionConfig.enabled = integration.value("enabled", false);
            sessionConfig.url = integration.value("url", std::string());
            sessionConfig.tokenEnvironment = integration.value("tokenEnvironment", sessionConfig.tokenEnvironment);
            const auto tokenFile = integration.value("tokenFile", std::string());
            if (!tokenFile.empty()) sessionConfig.tokenFile = ofToDataPath(tokenFile, true);
            const auto srt = config.value("srt", ofJson::object());
            srtHost = srt.value("host", std::string());
            portaSrt = srt.value("port", portaSrt);
            latenciaSrtMs = srt.value("latencyMs", latenciaSrtMs);
            const auto transfer = config.value("transfer", ofJson::object());
            transferConfig.enabled = transfer.value("enabled", transferConfig.enabled);
            transferConfig.sshHost = transfer.value("sshHost", transferConfig.sshHost);
            transferConfig.remoteDirectory = transfer.value("remoteDirectory", transferConfig.remoteDirectory);
            transferConfig.maxRetries = transfer.value("maxRetries", transferConfig.maxRetries);
            transferConfig.retryIntervalSeconds = transfer.value("retryIntervalSeconds", transferConfig.retryIntervalSeconds);
            transferConfig.bandwidthKbps = transfer.value("bandwidthKbps", transferConfig.bandwidthKbps);
            const auto thumbnail = config.value("thumbnail", ofJson::object());
            transferConfig.thumbnailEnabled = thumbnail.value("enabled", transferConfig.thumbnailEnabled);
            transferConfig.thumbnailWidth = thumbnail.value("width", transferConfig.thumbnailWidth);
            transferConfig.thumbnailTimestampSeconds = thumbnail.value("timestampSeconds", transferConfig.thumbnailTimestampSeconds);
        }
    }
    catch (const std::exception &e)
    {
        recordingConfigError = std::string("Erro em kinect-recording.json: ") + e.what();
    }
    adotarIpDaSessao();
    if (!srtHost.empty()) {
        if (ipValido(srtHost)) std::snprintf(ipMac, sizeof(ipMac), "%s", srtHost.c_str());
        else recordingConfigError = "srt.host deve ser o IPv4 do Mac";
    }
    if (recordingConfigError.empty()) {
        recordingTransfer.start(recordingConfig.outputRoot, recordingConfig.ffmpeg, transferConfig);
        sessionWebSocket.start(sessionConfig, recordingConfig.outputRoot);
        if (sessionConfig.enabled) FonteGlobalKinect::getInstance().ligaKinect();
    }
}

void GuiApp::registraBrisa(std::unique_ptr<Brisa> brisa)
{
    camadas.adiciona(std::move(brisa));
}

void GuiApp::exit()
{
    transmissor.parar();
    if (encerrada) return;
    // Libera os recursos gráficos enquanto há um contexto OpenGL disponível.
    auto anterior = ofGetMainLoop()->getCurrentWindow();
    auto janela = janelaRecursos.lock();
    if (janela)
    {
        ofGetMainLoop()->setCurrentWindow(janela);
        janela->makeCurrent();
    }
    camadas.clear();
    brisaFocada = 0;
    sessionWebSocket.shutdown();
    FonteGlobalKinect::getInstance().shutdown();
    recordingTransfer.shutdown();
    gui.reset();
    encerrada = true;
    if (anterior && anterior != janela)
    {
        ofGetMainLoop()->setCurrentWindow(anterior);
        anterior->makeCurrent();
    }
}

void GuiApp::update()
{
    if (encerrada) return;
    const float agora = ofGetElapsedTimef();
    const float dt = agora - ultimoUpdate;
    ultimoUpdate = agora;

    auto &fonte = FonteGlobalKinect::getInstance();
    fonte.setAngle(anguloKinect);
    fonte.setDepthClipping(depthClippingNear, depthClippingFar);
    for (const auto &event : sessionWebSocket.drain()) {
        sessionController.receive(event);
    }
    if (sessionController.takeCaptureRequest() && !fonte.getIsConnected()) fonte.ligaKinect();
    fonte.update();
    sincronizarClippingGravacao();
    sessionController.update(fonte.getRecorder(), recordingConfig, fonte.getIsConnected());
    sessionController.updateStreaming(transmissor, fonte.getIsConnected(), ipMac, portaSrt, latenciaSrtMs);
    if (sessionConfig.enabled && agora >= proximoEstadoSessao) {
        proximoEstadoSessao = agora + 0.2f;
        sessionWebSocket.publishState(sessionController.deviceState(fonte.getRecorder().status(),
            fonte.getIsConnected(), transmissor.fase() == FaseSrt::Transmitindo, transmissor.erro()));
    }
    for (const auto &brisa : camadas.getBrisas()) brisa->update(dt);
}

void GuiApp::draw()
{
    if (encerrada) return;
    ofBackground(100, 100, 100);

    gui->begin();


    // Janela para adicionar brisas
    ImGui::SetNextWindowSize(ofVec2f(550, 380), ImGuiCond_Appearing);
    adicionaBrisa();

    ImGui::SetNextWindowSize(ofVec2f(340, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ofVec2f(1060, 2), ImGuiCond_FirstUseEver);
    UIControlesGerais();

    AcaoBrisa acao = AcaoBrisa::Nenhuma;
    int i = 0;
    for (const auto &brisa : camadas.getBrisas())
    {
        const bool focada = brisa->getId() == brisaFocada;
        if (desenhaMiniaturas) brisa->desenhaMiniatura(i, focada);
        if (focada) acao = brisa->desenhaJanela(i);
        ++i;
    }
    gui->end();
    gui->draw();
    desenhaPreviaEFontesKinect();

    // ImGui já consumiu as texturas deste frame. Agora é seguro destruir camadas.
    if (acao == AcaoBrisa::Excluir)
    {
        camadas.remove(brisaFocada);
        brisaFocada = 0;
    }
    else if (acao == AcaoBrisa::Frente) camadas.move(brisaFocada, -1);
    else if (acao == AcaoBrisa::Tras) camadas.move(brisaFocada, 1);
}

void GuiApp::UIControlesGerais()
{
    ImGui::Begin("Controles Gerais");

    ImGui::Text("%.1f FPS (%.3f ms/frame) ", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    UIConexaoMac();
    ImGui::Separator();
    ImGui::Checkbox("Desenha miniaturas", &desenhaMiniaturas);

    if (ImGui::CollapsingHeader("Kinect"))
    {
        // Botões de liga e desliga do kinect
        if (FonteGlobalKinect::getInstance().getIsConnected())
        {
            ImGui::SliderInt("angulo", &anguloKinect, -30, 30);
            ImGui::SliderInt("depthClippingNear", &depthClippingNear, 500, 4000);
            ImGui::SliderInt("depthClippingFar", &depthClippingFar, 500, 4000);
            if (ImGui::Button("Desliga Kinect Global"))
            {
                FonteGlobalKinect::getInstance().desligaKinect();
            }
        }
        else
        {
            if (ImGui::Button("Liga Kinect Global"))
            {
                FonteGlobalKinect::getInstance().ligaKinect();
            }
        }
    }

    UIGravacaoKinect();

    if (ImGui::CollapsingHeader("Paleta de cores"))
    {
        for (int i = 0; i < coresPaleta.size(); i++)
        {
            string nomeCor = "Cor " + to_string(i + 1);
            ImGui::ColorEdit4(nomeCor.c_str(), (float *)&coresPaleta[i]);
            ImGui::SameLine();
            nomeCor = "[X] Excluir " + to_string(i + 1);
            if (ImGui::Button(nomeCor.c_str()))
            {
                coresPaleta.erase(coresPaleta.begin() + i);
            }
        }

        if (ImGui::Button(" + "))
        {
            coresPaleta.push_back(ofColor::fromHex(0xC0126D));
        }
    }

    ImGui::Text("Blend Mode");
    ImGui::RadioButton("alpha", &iBlend, 1);
    ImGui::SameLine();
    ImGui::RadioButton("add", &iBlend, 2);
    ImGui::SameLine();
    ImGui::RadioButton("screen", &iBlend, 3);

    // Pula uma linha
    ImGui::NewLine();

    // Existing composition PNG capture remains a separate feature.
    ImGui::Text("Captura PNG da composição");
    if (isRecording)
    {
        ImGui::Text("Recording... %d", framesRecorded);
        if (ImGui::Button("[STOP]"))
        {
            stopRecording();
        }
    }
    else
    {
        if (ImGui::Button("[REC]"))
        {
            startRecording();
        }
    }

    ImGui::Separator();
    ImGui::Text("Transmissão SRT");
    ImGui::Text("Destino: %s", ipMac);

    if (portaSrt < 1) portaSrt = 1;
    if (portaSrt > 65535) portaSrt = 65535;

    ImGui::InputInt("Porta", &portaSrt);
    ImGui::SliderInt("Latência (ms)", &latenciaSrtMs, 5, 200);
    if (ImGui::IsItemDeactivatedAfterEdit())
        transmissor.ajustarLatencia(latenciaSrtMs);

    const bool ligado = transmissor.estaLigado();
    if (!ligado)
    {
        if (ImGui::Button("Transmitir composição"))
        {
            sessionController.manualStreamingOverride();
            transmissor.solicitarInicio(ipMac, portaSrt, latenciaSrtMs);
        }
    }
    else if (ImGui::Button("Parar transmissão"))
    {
        sessionController.manualStreamingOverride();
        transmissor.solicitarParada();
    }
    if (ligado && sessionController.ownsStreaming() && ImGui::Button("Manter transmissão após sessão"))
        sessionController.manualStreamingOverride();

    const int portaUrl = ligado ? transmissor.portaEmUso() : portaSrt;
    const std::string urlTouch = transmissor.urlTouchDesigner(portaUrl, latenciaSrtMs);
    char urlBuffer[512];
    urlBuffer[0] = '\0';
    std::strncat(urlBuffer, urlTouch.c_str(), sizeof(urlBuffer) - 1);
    const float larguraUrl = ImGui::CalcTextSize(urlBuffer).x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().WindowPadding.x * 2 + 24;
    if (ImGui::GetWindowWidth() < larguraUrl)
        ImGui::SetWindowSize(ImVec2(larguraUrl, ImGui::GetWindowHeight()));
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##urlSrt", urlBuffer, sizeof(urlBuffer), ImGuiInputTextFlags_ReadOnly);

    const FaseSrt fase = transmissor.fase();
    if (ligado || fase == FaseSrt::Erro)
    {
        ImVec4 cor(0.85f, 0.85f, 0.85f, 1.0f);
        if (fase == FaseSrt::Transmitindo) cor = ImVec4(0.45f, 0.9f, 0.5f, 1.0f);
        else if (fase == FaseSrt::Aguardando) cor = ImVec4(0.95f, 0.8f, 0.35f, 1.0f);
        else if (fase == FaseSrt::Erro) cor = ImVec4(1.0f, 0.45f, 0.4f, 1.0f);
        const std::string estado = transmissor.status();
        ImGui::TextColored(cor, "%s", estado.c_str());
    }
    ImGui::End();
}

void GuiApp::UIConexaoMac()
{
    ImGui::Separator();
    ImGui::Text("Mac (Mephistopheles)");
    ImGui::InputText("IP do Mac", ipMac, sizeof(ipMac));
    if (ImGui::IsItemDeactivatedAfterEdit()) aplicaIpMac();
    if (!ipMacErro.empty())
        ImGui::TextColored(ImVec4(1.f, 0.45f, 0.4f, 1.f), "%s", ipMacErro.c_str());
    ImGui::TextWrapped("Este IP é o destino da transmissão SRT e do WebSocket.");
    if (sessionConfig.url.empty())
        ImGui::TextDisabled("WebSocket não configurado.");
    else
        ImGui::TextWrapped("WebSocket: %s", sessionConfig.url.c_str());
}

void GuiApp::adotarIpDaSessao()
{
    const std::string host = hostDaUrl(sessionConfig.url);
    if (!ipValido(host) || host == "127.0.0.1") return;
    std::snprintf(ipMac, sizeof(ipMac), "%s", host.c_str());
}

void GuiApp::aplicaIpMac()
{
    const std::string ip = limpaIp(ipMac);
    if (!ip.empty() && ip.size() < sizeof(ipMac))
        std::snprintf(ipMac, sizeof(ipMac), "%s", ip.c_str());
    if (!ipValido(ipMac))
    {
        ipMacErro = "IP inválido. Use quatro números, como 192.168.1.4.";
        return;
    }
    ipMacErro.clear();
    if (sessionConfig.url.empty()) return;

    const std::string atualizada = substituiHost(sessionConfig.url, ipMac);
    if (atualizada == sessionConfig.url) return;
    sessionConfig.url = atualizada;
    salvarUrlSessao();
    if (sessionConfig.enabled && recordingConfigError.empty())
        sessionWebSocket.start(sessionConfig, recordingConfig.outputRoot);
}

void GuiApp::salvarUrlSessao()
{
    const auto path = ofToDataPath("kinect-recording.json", true);
    ofFile arquivo(path);
    if (!arquivo.exists()) return;
    arquivo.close();
    std::string texto = ofBufferFromFile(path).getText();
    const std::string chave = "\"url\"";
    const auto pos = texto.find(chave);
    if (pos == std::string::npos)
    {
        ofLogWarning("GuiApp") << "kinect-recording.json não tem url para atualizar";
        return;
    }
    const auto colon = texto.find(':', pos + chave.size());
    const auto abre = colon == std::string::npos ? std::string::npos : texto.find('"', colon + 1);
    const auto fecha = abre == std::string::npos ? std::string::npos : texto.find('"', abre + 1);
    if (fecha == std::string::npos)
    {
        ofLogWarning("GuiApp") << "Não foi possível atualizar a url em kinect-recording.json";
        return;
    }
    texto.replace(abre + 1, fecha - abre - 1, sessionConfig.url);
    ofFile saida;
    if (!saida.open(path, ofFile::WriteOnly))
    {
        ofLogWarning("GuiApp") << "Não foi possível salvar " << path;
        return;
    }
    saida << texto;
}

void GuiApp::sincronizarClippingGravacao()
{
    // Keep the same normalization as FonteGlobalKinect::setDepthClipping.
    // start() snapshots this config; an ongoing recording keeps its own scale.
    recordingConfig.depthNearMm = std::max(0, depthClippingNear);
    recordingConfig.depthFarMm = std::max(recordingConfig.depthNearMm + 1, depthClippingFar);
}

void GuiApp::UIGravacaoKinect()
{
    if (!ImGui::CollapsingHeader("Gravação Kinect RGB + Depth", ImGuiTreeNodeFlags_DefaultOpen)) return;
    auto &source = FonteGlobalKinect::getInstance();
    auto &recorder = source.getRecorder();
    const auto state = recorder.status();
    const auto transfer = recordingTransfer.status();
    const bool conectado = source.getIsConnected();
    // Sliders may have changed in this draw, after the automatic-session update.
    sincronizarClippingGravacao();

    ImGui::Text("Kinect: %s", conectado ? "conectado" : "desconectado");
    ImGui::Text("Modo: %s", sessionController.isAutomatic() ? "automático / Corpo Expandido" : "manual");
    ImGui::TextWrapped("%s", sessionWebSocket.status().c_str());
    if (!sessionController.status().empty())
        ImGui::TextWrapped("%s", sessionController.status().c_str());
    if (!recordingConfigError.empty())
        ImGui::TextWrapped("%s", recordingConfigError.c_str());

    if (state.active)
    {
        ImGui::TextColored(ImVec4(1.f, 0.35f, 0.35f, 1.f), "%s", state.message.c_str());
        if (ImGui::Button("Parar gravação")) recorder.stop();
    }
    else if (recordingConfigError.empty())
    {
        if (!sessionController.manualStartAllowed())
            ImGui::TextWrapped("Gravação reservada à sessão automática.");
        else if (conectado)
        {
            if (ImGui::Button("Gravar Kinect")) recorder.start(recordingConfig);
        }
        else
            ImGui::TextWrapped("Conecte o Kinect para gravar RGB + Depth.");
        if (!state.message.empty()) ImGui::TextWrapped("%s", state.message.c_str());
    }

    if (state.active || state.bytes > 0)
    {
        const int elapsed = static_cast<int>(state.seconds);
        ImGui::Text("Tempo: %02d:%02d:%02d  |  %.3f GB / 10 GB", elapsed / 3600,
                    (elapsed / 60) % 60, elapsed % 60, state.bytes / 1e9);
    }
    if (!state.error.empty())
        ImGui::TextColored(ImVec4(1.f, 0.4f, 0.3f, 1.f), "%s", state.error.c_str());
    if (!transfer.error.empty())
        ImGui::TextWrapped("Último erro: %s", transfer.error.c_str());

    ImGui::Text("Transferências: %llu pendentes, %llu concluídas, %llu com falha",
                static_cast<unsigned long long>(transfer.pending),
                static_cast<unsigned long long>(transfer.completed),
                static_cast<unsigned long long>(transfer.failed));
    if (recordingConfigError.empty() && (transfer.failed > 0 || transfer.pending > 0))
    {
        if (ImGui::Button("Tentar transferências novamente")) recordingTransfer.retryFailed();
    }

    if (ImGui::CollapsingHeader("Mais informações"))
    {
        if (state.active)
        {
            ImGui::Text("FPS %d | RGB %d kb/s | Depth %d kb/s",
                        recordingConfig.fps, recordingConfig.rgbKbps, recordingConfig.depthKbps);
            ImGui::Text("Profundidade gravada: %d a %d mm", state.depthNearMm, state.depthFarMm);
        }
        else if (recordingConfigError.empty())
        {
            ImGui::SliderInt("FPS gravação", &recordingConfig.fps, 1, 30);
            ImGui::InputInt("RGB kb/s", &recordingConfig.rgbKbps);
            ImGui::InputInt("Depth kb/s", &recordingConfig.depthKbps);
            ImGui::Text("Profundidade do Kinect: %d a %d mm", recordingConfig.depthNearMm, recordingConfig.depthFarMm);
        }
        ImGui::TextWrapped("A gravação usa o clipping do Kinect no início. Ajustes durante a gravação valem para a próxima.");
        ImGui::Text("Descartados RGB/Depth: %llu / %llu",
                    static_cast<unsigned long long>(state.droppedRgb),
                    static_cast<unsigned long long>(state.droppedDepth));
        ImGui::Text("Repetidos RGB/Depth: %llu / %llu",
                    static_cast<unsigned long long>(state.repeatedRgb),
                    static_cast<unsigned long long>(state.repeatedDepth));
        if (!state.directory.empty()) ImGui::TextWrapped("%s", state.directory.c_str());
        if (!transfer.message.empty()) ImGui::TextWrapped("%s", transfer.message.c_str());
        if (recordingConfigError.empty() && transfer.failed == 0 && transfer.pending == 0)
        {
            if (ImGui::Button("Tentar transferências novamente")) recordingTransfer.retryFailed();
        }
    }
}

void GuiApp::desenhaPreviaEFontesKinect()
{
    glDisable(GL_SCISSOR_TEST);
    ofDisableDepthTest();
    ofEnableAlphaBlending();

    const float margem = 8.f;
    const float fonteW = 160.f;
    const float fonteH = 120.f;
    const float previaW = 308.f;
    const float previaH = 231.f;
    const float coluna = Brisa::larguraMiniatura + Brisa::bordaMiniatura * 2.f;

    const float larguraTela = static_cast<float>(ofGetWidth());
    const float alturaTela = static_cast<float>(ofGetHeight());
    const float kinectX = std::max(coluna + margem, larguraTela - margem - fonteW);
    const float depthY = std::max(margem, alturaTela - margem - fonteH);
    const float rgbY = std::max(margem, depthY - 4.f - fonteH);
    const ofRectangle blocoKinect(kinectX, rgbY, fonteW, depthY + fonteH - rgbY);

    if (desenhaMiniaturas && brisaFocada != 0)
    {
        Brisa *focada = nullptr;
        int indice = 0;
        int indiceFocada = -1;
        for (const auto &brisa : camadas.getBrisas())
        {
            if (brisa->getId() == brisaFocada)
            {
                focada = brisa.get();
                indiceFocada = indice;
                break;
            }
            ++indice;
        }
        const ofTexture *tex = focada ? focada->previaAmpliada() : nullptr;
        const bool temImagem = focada && (tex || focada->imgBtn.isAllocated());
        if (temImagem)
        {
            float x = kinectX - margem - previaW;
            float y = alturaTela - margem - previaH;
            if (x < coluna + margem)
            {
                x = std::max(coluna + margem, larguraTela - margem - previaW);
                y = rgbY - margem - previaH;
            }
            x = std::max(coluna + margem, x);
            y = std::max(margem, y);
            if (ofRectangle(x, y, previaW, previaH).intersects(blocoKinect))
                y = std::max(margem, rgbY - margem - previaH);

            ofPushStyle();
            ofSetColor(205, 100, 100);
            ofDrawRectangle(x - 2, y - 2, previaW + 4, previaH + 4);
            ofSetColor(255);
            if (tex) tex->draw(x, y, previaW, previaH);
            else focada->imgBtn.draw(x, y, previaW, previaH);
            const std::string rotulo = (tex ? "Kinect da brisa #" : "Brisa #") + ofToString(indiceFocada + 1);
            ofDrawBitmapStringHighlight(rotulo, x + 6, y + 16, ofColor(0, 0, 0, 170), ofColor(255));
            ofPopStyle();
        }
    }

    auto &fonte = FonteGlobalKinect::getInstance();
    auto desenhaFonte = [&](const ofTexture &textura, float y, const char *nome)
    {
        if (!textura.isAllocated()) return;
        ofPushStyle();
        ofSetColor(255);
        textura.draw(kinectX, y, fonteW, fonteH);
        ofDrawBitmapStringHighlight(nome, kinectX + 4, y + 14, ofColor(0, 0, 0, 170), ofColor(255));
        ofPopStyle();
    };
    desenhaFonte(fonte.getRgbTexture(), rgbY, "Kinect RGB");
    desenhaFonte(fonte.getDepthTexture(), depthY, "Kinect profundidade");
}

void GuiApp::setFrameCountRecorded(int frameCount)
{
    this->framesRecorded = frameCount;
}

void GuiApp::startRecording()
{
    isRecording = true;
    framesRecorded = 0;
}

void GuiApp::stopRecording()
{
    isRecording = false;
}

void GuiApp::adicionaBrisa()
{

    ImGui::SetNextWindowSize(ofVec2f(570, 340), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ofVec2f(170, 2), ImGuiCond_FirstUseEver);
    ImGui::Begin("Adicione uma camada de brisa:");
    bool criaColunas = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaColunas, ImVec2(120, 90));
    ImGui::SameLine();
    bool criaKinect = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaKinect, ImVec2(120, 90));
    ImGui::SameLine();
    bool criaKinectCam = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaKinectCam, ImVec2(120, 90));
    ImGui::SameLine();
    bool criaMatriz = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaMatriz, ImVec2(120, 90));
    ImGui::SameLine();
    bool criaSombras = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaSombras, ImVec2(120, 90));

    bool criaGif = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaGif, ImVec2(120, 90));
    ImGui::SameLine();
    bool criaImg = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaImg, ImVec2(120, 90));
    ImGui::SameLine();
    bool criaPoligono = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaPoligono, ImVec2(120, 90));
    ImGui::SameLine();
    bool criaVideo = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaVideo, ImVec2(120, 90));

    bool criaCamera = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaCamera, ImVec2(120, 90));
    // ImGui::SameLine();
    // bool criaFinder = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaFinder, ImVec2(120, 90));
    // ImGui::SameLine();
    // bool criaBody = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaBody, ImVec2(120, 90));
    //    bool criaMandala = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaMandala, ImVec2(120, 90)); ImGui::SameLine();
    //    bool criaBorda = ImGui::ImageButton((ImTextureID)(uintptr_t)btnCriaBorda, ImVec2(120, 90));

    if (criaColunas)
    {
        cout << "btn pressionado: criaColunas";
        registraBrisa(std::make_unique<Colunas>());
    }
    if (criaVideo)
    {
        cout << "btn pressionado: criaVideo";
        registraBrisa(std::make_unique<VideoBrisa>());
    }
    if (criaPoligono)
    {
        cout << "btn pressionado: criaPoligono";
        registraBrisa(std::make_unique<PoligonoBrisa>(coresPaleta));
    }
    if (criaKinect)
    {
        cout << "btn pressionado: criaKinect";
        registraBrisa(std::make_unique<KinectContornosBrisa>());
    }
    if (criaImg)
    {
        cout << "btn pressionado: criaImg";
        registraBrisa(std::make_unique<ImagemBrisa>());
    }
    if (criaGif)
    {
        cout << "btn pressionado: criaGif";
        registraBrisa(std::make_unique<GifBrisa>());
    }
    if (criaMatriz)
    {
        cout << "btn pressionado: criaMatriz";
        registraBrisa(std::make_unique<MatrizBrisa>(coresPaleta));
    }
    if (criaSombras)
    {
        cout << "btn pressionado: criaSombras";
        registraBrisa(std::make_unique<SombraBrisa>(coresPaleta));
    }
    if (criaKinectCam)
    {
        cout << "btn pressionado: criaKinectCam";
        registraBrisa(std::make_unique<KinectCamBrisa>());
    }
    if (criaCamera)
    {
        cout << "btn pressionado: criaCamera";
        registraBrisa(std::make_unique<CameraBrisa>());
    }
    ImGui::End();
}

void GuiApp::mousePressed(int x, int y, int iButton)
{
    if (encerrada || !gui || !desenhaMiniaturas || ImGui::GetIO().WantCaptureMouse)
    {
        return;
    }

    for (int i = 0; i < static_cast<int>(camadas.getBrisas().size()); ++i)
    {
        const ofRectangle limites = Brisa::limitesMiniatura(i);
        if (x >= limites.x && x < limites.getRight() &&
            y >= limites.y && y < limites.getBottom())
        {
            brisaFocada = camadas.getBrisas()[i]->getId();
            break;
        }
    }
}

void GuiApp::windowResized(int w, int h)
{
    WindowManager::getInstance().setGuiWindowSize(w, h);
}
