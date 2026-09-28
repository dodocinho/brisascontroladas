#ifndef FINDERBRISA_H__
#define FINDERBRISA_H__

#include "../Brisa.h"

class FinderBrisa : public Brisa {

    public:
    FinderBrisa(vector<ImVec4> *coresPaleta);
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