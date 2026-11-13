#include "../Brisa.h"
#include "ProcessamentoKinect.h"
class KinectContornosBrisa : public Brisa
{

public:
    explicit KinectContornosBrisa(bool contornos = true, bool espelhoHorizontal = false);
    void draw();
    const ofTexture *previaAmpliada() const override;
    void update(float dt);

    void drawControles(int iBrisa);

    float nivelFade = 20;
    bool ligaContornos = true;
    bool bFindHoles = false;
    int minArea, maxArea, blobsConsiderados;
    ofxCvContourFinder contourFinder;
    ofxCvGrayscaleImage grayImage;
    ofxCvGrayscaleImage bordas;

    ProcessamentoKinect processamento{ProcessamentoKinect::Canal::Profundidade};
};
