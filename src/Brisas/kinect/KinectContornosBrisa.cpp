#include "KinectContornosBrisa.h"
#include "FonteGlobalKinect.h"

KinectContornosBrisa::KinectContornosBrisa()
{
    setup();
    // Configura a brisa e defini o ícone
    mirrorHorizontal = mirrorVertical = false;
    ofSetBackgroundAuto(false);
    minArea = 10;
    maxArea = (640 * 480) / 2; // (kinect_width*kinect_height)/2
    blobsConsiderados = 10;
}

void KinectContornosBrisa::update(float dt)
{
    const auto &depth = FonteGlobalKinect::getInstance().getDepthPixels();
    if (!depth.isAllocated())
    {
        limpaFrame();
        return;
    }
    fboBrisa.begin();

    if (clearFrames)
    {
        // Define um alpha de acordo com o nivel Fade
        ofSetColor(ofFloatColor(0, 0, 0, 0.05 * (nivelFade / 100)));
        ofFill();
        ofDrawRectangle(0, 0, fboBrisa.getWidth(), fboBrisa.getHeight());
    }
    ofSetColor(255, 255, 255, 255);

    if (ligaContornos)
    {
        grayImage.setFromPixels(depth);
        contourFinder.findContours(grayImage, minArea, maxArea, blobsConsiderados, bFindHoles);
        contourFinder.draw(0, 0, fboBrisa.getWidth(), fboBrisa.getHeight());
    }

    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void KinectContornosBrisa::draw()
{
    Brisa::draw();
}

void KinectContornosBrisa::drawControles(int iBrisa)
{
    ImGui::Checkbox("Liga Fade", &clearFrames);
    if (clearFrames)
    {
        ImGui::SliderFloat("Fade", &nivelFade, 0, 100);
    }

    ImGui::Checkbox("Contornos", &ligaContornos);
    ImGui::Checkbox("Encontrar buracos", &bFindHoles);
    ImGui::SliderInt("min Area", &minArea, 0, 100);
    ImGui::SliderInt("max Area", &maxArea, 100, (640 * 480));
    ImGui::SliderInt("blobs", &blobsConsiderados, 0, 20);
}
