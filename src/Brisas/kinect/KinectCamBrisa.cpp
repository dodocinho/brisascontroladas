#include "KinectCamBrisa.h"

KinectCamBrisa::KinectCamBrisa() {
    setup();
    // Configura a brisa e defini o ícone
    mirrorHorizontal = mirrorVertical = false;
    clearFrames = true;
    nivelFade = 0;
    ofSetBackgroundAuto(false);
    minArea = 10;
    maxArea = (640*480)/2; // (kinect_width*kinect_height)/2
    blobsConsiderados = 10;
    bFindHoles = false;
}

void KinectCamBrisa::update( float dt ) {
    if (!processamento.update())
    {
        limpaFrame();
        return;
    }
    fboBrisa.begin();

    if (clearFrames) {
        // Define um alpha de acordo com o nivel Fade
        ofSetColor( ofFloatColor(1.0, 1.0, 1.0, 0.05*(nivelFade/100) ) );
    } else {
        ofSetColor(255,255,255,255);
    }

    processamento.draw(0, 0, fboBrisa.getWidth(), fboBrisa.getHeight());
    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void KinectCamBrisa::draw() {
    Brisa::draw();
}

const ofTexture *KinectCamBrisa::previaAmpliada() const
{
    return processamento.texturaPronta();
}

void KinectCamBrisa::drawControles(int iBrisa) {
    ImGui::Checkbox("Liga Fade", &clearFrames);
    if(clearFrames){
        ImGui::SliderFloat("Fade", &nivelFade, 0, 100);
    }

    ImGui::Checkbox("Encontrar buracos", &bFindHoles);
    ImGui::SliderInt("min Area", &minArea, 0, 100);
    ImGui::SliderInt("max Area", &maxArea, 100, (640*480) );
    ImGui::SliderInt("blobs", &blobsConsiderados, 0, 20);

    processamento.drawControles();
}
