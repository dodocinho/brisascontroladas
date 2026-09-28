#include "SombraBrisa.h"

SombraBrisa::SombraBrisa(const vector<ImVec4> &coresPaleta) {
    setup();

    mirrorHorizontal = mirrorVertical = false;
    desenhaBlur = true;
    blurKinect = false;
    brightnessGray = 0.5;
    contrastGray = 0.95;
    iBlur = 2;
    sombraHoriz = true;
    sombraVert = false;

    corBrisa = ofFloatColor::white;
    corComplementar = ofFloatColor::black;
    if (!coresPaleta.empty())
    {
        const size_t iCor1 = static_cast<size_t>(ofRandom(coresPaleta.size()));
        corBrisa = coresPaleta[iCor1];
        corComplementar = coresPaleta[(iCor1 + 1) % coresPaleta.size()];
    }

    processamento.setBlur(20);
}

void SombraBrisa::update( float dt ) {
    if (!processamento.update())
    {
        limpaFrame();
        return;
    }
    fboBrisa.begin();

    if (clearFrames) {
        ofClear(255,255,255, 0);
    }

    ofPixels pixels = processamento.getPixels();
    pixels.setImageType(OF_IMAGE_GRAYSCALE);
    pixels.resize(fboBrisa.getWidth(), fboBrisa.getHeight());
    grayImage.setFromPixels(pixels);

    grayImage.mirror(mirrorVertical, mirrorHorizontal);
    grayImage.brightnessContrast(brightnessGray, contrastGray);

    blurGray = grayImage;
    for (int i = 0; i < iBlur; i++) {
        blurGray.erode_3x3();
        blurGray.dilate_3x3();
        blurGray.blurHeavily();
        blurGray.erode_3x3();
        blurGray.dilate_3x3();
        blurGray.blur(3);
    }
    ofSetColor(ofFloatColor(corBrisa));
    blurGray.draw(0, 0, WIDTH, HEIGHT);
    // Desenha as sombras repetidas (efeito caleidoscopio)
    if (sombraHoriz || sombraVert) {
        ofEnableBlendMode(OF_BLENDMODE_ADD);
        if (sombraVert) {
            ofSetColor(ofFloatColor(corComplementar));
            blurGray.mirror(true, false);
            blurGray.draw(0, 0, WIDTH, HEIGHT);
        }
        if (sombraHoriz) {
            ofSetColor(ofFloatColor(corComplementar));
            blurGray.mirror(false, true);
            blurGray.draw(0, 0, WIDTH, HEIGHT);
        }
        if (sombraVert && sombraHoriz) {
            ofSetColor(ofFloatColor(corBrisa));
            blurGray.mirror(true, false);
            blurGray.draw(0, 0, WIDTH, HEIGHT);
        }
    }

    grayPixels = grayImage.getPixels();

    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void SombraBrisa::draw() {
    Brisa::draw();
}

void SombraBrisa::drawControles(int iBrisa) {
    ImGui::ColorEdit4("Base", (float*)&corBrisa);
    ImGui::ColorEdit4("Complementar", (float*)&corComplementar);

    // Camêra fonte
    ImGui::Text("Sombras");
    ImGui::Checkbox("--", &sombraHoriz);
    ImGui::Checkbox("|", &sombraVert);

    ImGui::Checkbox("Limpa Frames", &clearFrames);

    processamento.drawControles();
    processamento.drawMiniatura();
}
