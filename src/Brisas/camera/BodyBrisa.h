#ifndef BODYBRISA_H__
#define BODYBRISA_H__

#include "../Brisa.h"

class BodyBrisa : public Brisa {

    public:
    BodyBrisa(vector<ImVec4> *coresPaleta);
    void draw();
    void update( float dt );

    void drawControles(int iBrisa);

    ofVideoGrabber cam;
    ofxCvHaarFinder finderBody;
    ofxCvHaarFinder finderUpperBody;
    ofxCvHaarFinder finderSmile;
    ofxCvHaarFinder finderFace;
    ofxCvColorImage colorImg;
    int indexDevice;
    bool mirrorHorizontal, mirrorVertical;
};

#endif