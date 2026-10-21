#include "../Brisa.h"
#include "ProcessamentoKinect.h"
class KinectContornosBrisa : public Brisa
{

public:
    KinectContornosBrisa();
    void draw();
    void update(float dt);

    void drawControles(int iBrisa);

    float nivelFade = 20;
    bool ligaContornos = true;
    bool bFindHoles = false;
    bool mirrorHorizontal, mirrorVertical;
    int minArea, maxArea, blobsConsiderados;
    ofxCvContourFinder contourFinder;
    ofxCvGrayscaleImage grayImage;
    ofxCvGrayscaleImage bordas;

    ProcessamentoKinect processamento{ProcessamentoKinect::Canal::Profundidade};
};