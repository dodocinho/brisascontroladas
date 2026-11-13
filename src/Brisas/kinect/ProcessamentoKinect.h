#pragma once

#include "ofMain.h"
#include "ofxOpenCv.h"

// Estado visual de uma camada. Nunca abre nem atualiza o aparelho.
class ProcessamentoKinect
{
public:
    enum class Canal { Rgb = 1, Profundidade = 2 };
    explicit ProcessamentoKinect(Canal canal) : canal(canal) {}
    bool update();
    bool processa(const ofPixels &pixels);
    void draw(float x, float y, float width, float height);
    void drawControles();
    const ofTexture *texturaPronta() const { return textura.isAllocated() ? &textura : nullptr; }
    void setBlur(int valor) { blur = valor; }
    void setMirrorHorizontal(bool valor) { mirrorHorizontal = valor; }
    const ofPixels &getPixels() const { return resultado; }

private:
    Canal canal;
    Canal canalAnterior = Canal::Profundidade;
    int blur = 0, blurForte = 0, erode = 0, dilate = 0, rastro = 0;
    bool mirrorHorizontal = false, mirrorVertical = false;
    float brilho = 0.5f, contraste = 0.5f;
    ofxCvGrayscaleImage grayImage;
    ofPixels resultado;
    cv::Mat acumulado;
    ofTexture textura;
};
