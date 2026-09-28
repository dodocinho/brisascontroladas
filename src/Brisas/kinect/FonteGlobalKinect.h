#pragma once

#include "ofMain.h"
#include "ofxKinect.h"
#include <memory>

// Captura única, atualizada por GuiApp antes das brisas, na thread principal.
// As referências retornadas só devem ser usadas até a próxima atualização.
class FonteGlobalKinect
{
public:
    static FonteGlobalKinect &getInstance();
    FonteGlobalKinect(const FonteGlobalKinect &) = delete;
    FonteGlobalKinect &operator=(const FonteGlobalKinect &) = delete;

    void ligaKinect();
    void desligaKinect();
    void update();
    void shutdown();
    bool getIsConnected() const;
    void setAngle(int angle);
    void setDepthClipping(int near, int far);

    const ofPixels &getDepthPixels() const { return depthPixels; }
    const ofPixels &getRgbPixels() const { return rgbPixels; }
    const ofTexture &getDepthTexture() const { return depthTexture; }
    const ofTexture &getRgbTexture() const { return rgbTexture; }

private:
    FonteGlobalKinect() = default;
    ~FonteGlobalKinect() = default;
    void limpaFrames();
    void atualizaDemo();
    void abreCamera();

    std::unique_ptr<ofxKinect> kinect;
    std::unique_ptr<ofVideoPlayer> demoDepth, demoRgb;
    ofPixels depthPixels, rgbPixels;
    ofTexture depthTexture, rgbTexture;
    bool tentouDemo = false;
    bool encerrada = false;
    bool usandoCamera = false;
    enum class Pedido { Nenhum, Ligar, Desligar };
    Pedido pedido = Pedido::Nenhum;
    int angulo = 0;
    int cortePerto = 500, corteLonge = 2000;
    bool configuraCamera = true;
};
