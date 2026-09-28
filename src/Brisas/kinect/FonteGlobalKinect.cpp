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
    if (pedido == Pedido::Desligar) kinect.reset();
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
    if (kinect->isFrameNewDepth())
    {
        depthPixels = kinect->getDepthPixels();
        if (depthPixels.isAllocated()) depthTexture.loadData(depthPixels);
    }
    if (kinect->isFrameNewVideo())
    {
        rgbPixels = kinect->getPixels();
        if (rgbPixels.isAllocated()) rgbTexture.loadData(rgbPixels);
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
            rgbTexture.loadData(rgbPixels);
        }
    }
}

void FonteGlobalKinect::shutdown()
{
    // Chamado antes de destruir as janelas: nenhum recurso GL fica no singleton.
    encerrada = true;
    pedido = Pedido::Nenhum;
    kinect.reset();
    demoDepth.reset();
    demoRgb.reset();
    limpaFrames();
    usandoCamera = false;
}
