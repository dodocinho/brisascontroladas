#ifndef KINECTCAMBRISA_H__
#define KINECTCAMBRISA_H__

#include "../Brisa.h"
#include "ProcessamentoKinect.h"

class KinectCamBrisa : public Brisa {

    float nivelFade;
    public:
    KinectCamBrisa();
    void draw() override;
    const ofTexture *previaAmpliada() const override;
    void update(float dt) override;


    void drawControles(int iBrisa) override;

    bool mirrorHorizontal, mirrorVertical;
    bool bFindHoles;
    int minArea,maxArea,blobsConsiderados;
    ofxCvContourFinder contourFinder;
    ofxCvGrayscaleImage grayImage;

    ProcessamentoKinect processamento{ProcessamentoKinect::Canal::Rgb};
};

#endif