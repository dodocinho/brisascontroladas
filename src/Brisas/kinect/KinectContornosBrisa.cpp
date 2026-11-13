#include "KinectContornosBrisa.h"

KinectContornosBrisa::KinectContornosBrisa(bool contornos, bool espelhoHorizontal)
    : ligaContornos(contornos)
{
    setup();
    // Configura a brisa e defini o ícone
    processamento.setMirrorHorizontal(espelhoHorizontal);
    ofSetBackgroundAuto(false);
    minArea = 10;
    maxArea = (640 * 480) / 2; // (kinect_width*kinect_height)/2
    blobsConsiderados = 10;
}

void KinectContornosBrisa::update(float dt)
{
    if (!processamento.update())
    {
        limpaFrame();
        return;
    }
    // Brilho, contraste e os outros filtros já estão nesta cópia.
    ofPixels cinza = processamento.getPixels();
    cinza.setImageType(OF_IMAGE_GRAYSCALE);
    if (cinza.getWidth() == 0 || cinza.getHeight() == 0)
    {
        limpaFrame();
        return;
    }
    grayImage.setFromPixels(cinza.getData(), cinza.getWidth(), cinza.getHeight());

    fboBrisa.begin();
    ofEnableAlphaBlending();

    if (clearFrames)
    {
        // Escurece o quadro anterior de acordo com o nível do fade.
        ofSetColor(ofFloatColor(0, 0, 0, 0.05 * (nivelFade / 100)));
        ofFill();
        ofDrawRectangle(0, 0, fboBrisa.getWidth(), fboBrisa.getHeight());
    }
    else
    {
        // Sem fade, nada do quadro anterior permanece.
        ofClear(0, 0, 0, 0);
    }
    ofSetColor(255, 255, 255, 255);

    if (ligaContornos)
    {
        cv::Mat suavizada, detectadas;
        cv::GaussianBlur(grayImage.getCvMat(), suavizada, cv::Size(5, 5), 0);
        cv::Canny(suavizada, detectadas, 40, 120);
        bordas.setFromPixels(detectadas.ptr<unsigned char>(), detectadas.cols, detectadas.rows);
        contourFinder.findContours(bordas, minArea, maxArea, blobsConsiderados, bFindHoles);
        contourFinder.draw(0, 0, fboBrisa.getWidth(), fboBrisa.getHeight());
    }
    else
    {
        grayImage.draw(0, 0, fboBrisa.getWidth(), fboBrisa.getHeight());
    }

    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void KinectContornosBrisa::draw()
{
    Brisa::draw();
}

const ofTexture *KinectContornosBrisa::previaAmpliada() const
{
    return processamento.texturaPronta();
}

void KinectContornosBrisa::drawControles(int iBrisa)
{
    ImGui::Checkbox("Liga Fade", &clearFrames);
    if (clearFrames)
    {
        ImGui::SliderFloat("Fade", &nivelFade, 0, 100);
    }

    ImGui::Checkbox("Contornos", &ligaContornos);
    if (ligaContornos)
    {
        ImGui::Checkbox("Encontrar buracos", &bFindHoles);
        ImGui::SliderInt("min Area", &minArea, 0, 100);
        ImGui::SliderInt("max Area", &maxArea, 100, (640 * 480));
        ImGui::SliderInt("blobs", &blobsConsiderados, 0, 20);
    }

    processamento.drawControles();
}
