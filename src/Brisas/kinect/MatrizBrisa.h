#include "../Brisa.h"
#include "ProcessamentoKinect.h"

class MatrizBrisa : public Brisa {


    public:
    MatrizBrisa(vector<ImVec4> coresPaleta);
    void draw() override;
    const ofTexture *previaAmpliada() const override;
    void update(float dt) override;

    void drawControles(int iBrisa) override;
    void desenhaPonto(int brilho, int width, int height, int x, int y, int gapX, int gapY);
    void desenhaPixels(int brilho, int width, int height, int x, int y, int gapX, int gapY);
    void desenhaColunas(int brilho, int width, int height, int x, int y, int gapX, int gapY);
    bool blurKinect, desenhaBlur, mirrorHorizontal, mirrorVertical;
    float brightnessGray, contrastGray;
    ofPixels grayPixels;
    int intervaloX, intervaloY;
    bool intercalaPontos;
    int tipoMatriz;

    vector<ImVec4> coresPaleta;

    ofxCvGrayscaleImage grayImage, blurGray;
    ProcessamentoKinect processamento{ProcessamentoKinect::Canal::Profundidade};
};
