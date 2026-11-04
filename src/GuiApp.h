#pragma once

#include "ofxImGui.h"
#include "ofMain.h"
#include "ofxOpenCv.h"
#include "ofxOsc.h"
#include "Brisas/Brisa.h"
#include "Brisas/ColecaoBrisas.h"
#include "camera/CameraBrisa.h"
#include "camera/FinderBrisa.h"
// #include "camera/BodyBrisa.h"
#include "kinect/KinectCamBrisa.h"
#include "kinect/KinectContornosBrisa.h"
#include "kinect/SombraBrisa.h"
#include "kinect/MatrizBrisa.h"

#include "Brisas/kinect/FonteGlobalKinect.h"
#include "kinect/Colunas.h"
#include "TransmissorSrt.h"
#include "RecordingTransfer.h"
#include "SessionWebSocketClient.h"
#include "SessionRecordingController.h"

class GuiApp : public ofBaseApp
{
public:
    void setup();
    void update();
    void draw();
    GuiApp() = default;
    void exit();
    ~GuiApp() override = default;
    GuiApp(const GuiApp &) = delete;
    GuiApp &operator=(const GuiApp &) = delete;
    void mousePressed(int x, int y, int iButton);


    void adicionaBrisa();
    void registraBrisa(std::unique_ptr<Brisa> brisa);
    const std::vector<std::unique_ptr<Brisa>> &getBrisas() const { return camadas.getBrisas(); }
    void windowResized(int w, int h);
    void UIControlesGerais();
    void UIConexaoMac();
    void UIGravacaoKinect();
    bool isRecording = false;
    void startRecording();
    void stopRecording();
    void setFrameCountRecorded(int frameCount);
    int framesRecorded = 0;

    char ipMac[64] = "192.168.1.4";
    std::string ipMacErro;
    int portaSrt = 9494;
    int latenciaSrtMs = TransmissorSrt::latenciaPadraoMs;
    TransmissorSrt transmissor;



    // Kinect
    int anguloKinect;
    int depthClippingFar, depthClippingNear;

    GLuint btnCriaVideo,
        btnCriaPoligono,
        btnCriaKinect,
        btnCriaImg,
        btnCriaFinder,
        btnCriaGif,
        btnCriaMatriz,
        btnCriaSombras,
        btnCriaMandala,
        btnCriaBorda,
        btnCriaCamera,
        btnCriaKinectCam,
        btnCriaBody,
        btnCriaColunas;

    vector<ImVec4> coresPaleta;
    ofBlendMode blendMode;
    int iBlend;
    bool desenhaMiniaturas;

private:
    KinectRecorder::Config recordingConfig;
    RecordingTransfer recordingTransfer;
    SessionWebSocketClient sessionWebSocket;
    SessionWebSocketClient::Config sessionConfig;
    SessionRecordingController sessionController;
    RecordingTransfer::Config transferConfig;
    std::string recordingConfigError;
    void adotarIpDaSessao();
    void aplicaIpMac();
    void salvarUrlSessao();
    void desenhaPreviaEFontesKinect();
    std::unique_ptr<ofxImGui::Gui> gui;
    std::weak_ptr<ofAppBaseWindow> janelaRecursos;
    ColecaoBrisas camadas;
    BrisaId brisaFocada = 0;
    float ultimoUpdate = 0;
    bool encerrada = false;
};
