#include "FonteGlobalKinect.h"
#include <algorithm>

FonteGlobalKinect &FonteGlobalKinect::getInstance()
{
    static FonteGlobalKinect instance;
    return instance;
}

bool FonteGlobalKinect::getIsConnected() const
{
    return kinect && kinect->isConnected();
}

void FonteGlobalKinect::ligaKinect()
{
    if (!encerrada) pedido = Pedido::Ligar;
}

void FonteGlobalKinect::desligaKinect()
{
    pedido = Pedido::Desligar;
}

void FonteGlobalKinect::setAngle(int angle)
{
    angle = std::max(-30, std::min(30, angle));
    if (angulo != angle) { angulo = angle; configuraCamera = true; }
}

void FonteGlobalKinect::setDepthClipping(int near, int far)
{
    near = std::max(0, near);
    far = std::max(near + 1, far);
    if (cortePerto != near || corteLonge != far)
    {
        cortePerto = near;
        corteLonge = far;
        configuraCamera = true;
    }
}

void FonteGlobalKinect::setMirror(bool horizontal, bool vertical)
{
    mirrorHorizontal = horizontal;
    mirrorVertical = vertical;
}

void FonteGlobalKinect::abreCamera()
{
    if (getIsConnected()) return;
    kinect = std::make_unique<ofxKinect>();
    kinect->setRegistration(true);
    // O singleton mantém as texturas; o driver entrega somente pixels.
    if (!kinect->init(false, true, false) || !kinect->open())
    {
        ofLogWarning("FonteGlobalKinect") << "Kinect indisponível; usando demonstração quando disponível.";
        kinect.reset();
        return;
    }
    configuraCamera = true;
}

void FonteGlobalKinect::limpaFrames()
{
    depthPixels.clear();
    rgbPixels.clear();
    depthTexture.clear();
    rgbTexture.clear();
}

void FonteGlobalKinect::update()
{
    if (encerrada) return;
    if (pedido == Pedido::Ligar) abreCamera();
    if (pedido == Pedido::Desligar)
    {
        recorder.stop("kinect_disconnected");
        kinect.reset();
    }
    pedido = Pedido::Nenhum;

    bool conectada = getIsConnected();
    if (conectada != usandoCamera)
    {
        limpaFrames();
        usandoCamera = conectada;
        if (demoDepth) demoDepth->setPaused(conectada);
        if (demoRgb) demoRgb->setPaused(conectada);
    }
    if (!conectada)
    {
        recorder.stop("kinect_disconnected");
        atualizaDemo();
        return;
    }
    if (configuraCamera)
    {
        kinect->setCameraTiltAngle(angulo);
        kinect->setDepthClipping(cortePerto, corteLonge);
        configuraCamera = false;
    }
    kinect->update();
    // ofxKinect uses FREENECT_DEPTH_REGISTERED (millimeters) with registration on.
    // The addon discards hardware timestamps: this is host receipt time, shared
    // by new images exposed in this update, not a claim of exposure-level sync.
    const auto receivedNs = KinectRecorder::monotonicNs();
    if (recorder.accepting())
    {
        // O gravador recebe os originais e aplica o espelho fixado no início da sessão.
        if (kinect->isFrameNewDepth())
        {
            const auto &mm = kinect->getRawDepthPixels();
            recorder.submitDepthMm(mm.getData(), mm.size(), receivedNs);
        }
        if (kinect->isFrameNewVideo())
        {
            const auto &rgb = kinect->getPixels();
            recorder.submitRgb(rgb.getData(), rgb.size(), receivedNs);
        }
    }
    if (kinect->isFrameNewDepth())
    {
        depthPixels = kinect->getDepthPixels();
        if (depthPixels.isAllocated())
        {
            depthPixels.mirror(mirrorVertical, mirrorHorizontal);
            depthTexture.loadData(depthPixels);
        }
    }
    if (kinect->isFrameNewVideo())
    {
        rgbPixels = kinect->getPixels();
        if (rgbPixels.isAllocated())
        {
            rgbPixels.mirror(mirrorVertical, mirrorHorizontal);
            rgbTexture.loadData(rgbPixels);
        }
    }
}

void FonteGlobalKinect::atualizaDemo()
{
    if (!tentouDemo)
    {
        tentouDemo = true;
        auto carrega = [](const std::string &caminho) {
            if (!ofFile::doesFileExist(caminho)) return std::unique_ptr<ofVideoPlayer>{};
            auto video = std::make_unique<ofVideoPlayer>();
            video->setUseTexture(false);
            if (!video->load(caminho)) return std::unique_ptr<ofVideoPlayer>{};
            video->setLoopState(OF_LOOP_NORMAL);
            video->play();
            return video;
        };
        demoDepth = carrega("kinectdemo/depthdance.mp4");
        demoRgb = carrega("kinectdemo/rgbdance.mp4");
    }
    if (demoDepth)
    {
        demoDepth->update();
        if (demoDepth->isFrameNew() && demoDepth->getPixels().isAllocated())
        {
            depthPixels = demoDepth->getPixels();
            depthPixels.setImageType(OF_IMAGE_GRAYSCALE);
            depthPixels.mirror(mirrorVertical, mirrorHorizontal);
            depthTexture.loadData(depthPixels);
        }
    }
    if (demoRgb)
    {
        demoRgb->update();
        if (demoRgb->isFrameNew() && demoRgb->getPixels().isAllocated())
        {
            rgbPixels = demoRgb->getPixels();
            rgbPixels.setImageType(OF_IMAGE_COLOR);
            rgbPixels.mirror(mirrorVertical, mirrorHorizontal);
            rgbTexture.loadData(rgbPixels);
        }
    }
}

void FonteGlobalKinect::shutdown()
{
    // Chamado antes de destruir as janelas: nenhum recurso GL fica no singleton.
    recorder.shutdown();
    encerrada = true;
    pedido = Pedido::Nenhum;
    kinect.reset();
    demoDepth.reset();
    demoRgb.reset();
    limpaFrames();
    usandoCamera = false;
}
