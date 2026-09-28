#ifndef CAMBRISA_H__
#define CAMBRISA_H__

#include "../Brisa.h"

class CameraBrisa : public Brisa {

    public:
    CameraBrisa();
    void draw();
    void update( float dt );

    void drawControles(int iBrisa) override;

    ofVideoGrabber cam;
    int indexDevice;
    bool mirrorHorizontal, mirrorVertical;
    int camOriginalWidth, camOriginalHeight;
    int camWidth, camHeight;
    int camX, camY;
};

#endif