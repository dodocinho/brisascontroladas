#include "../Brisa.h"
#include "ProcessamentoKinect.h"

class SombraBrisa : public Brisa {


    public:
    SombraBrisa(const vector<ImVec4> &coresPaleta);
    void draw() override;
    void update(float dt) override;

    void drawControles(int iBrisa) override;
    bool blurKinect, desenhaBlur, mirrorHorizontal, mirrorVertical;
    float brightnessGray, contrastGray;
    int iBlur;
    bool sombraHoriz, sombraVert;
    ImVec4 corBrisa, corComplementar;
    ofPixels grayPixels;

    ofxCvGrayscaleImage grayImage, blurGray;

    ProcessamentoKinect processamento{ProcessamentoKinect::Canal::Profundidade};
};
