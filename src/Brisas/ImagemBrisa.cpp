#include "Brisa.h"

ImagemBrisa::ImagemBrisa() {
    Brisa::setup();
    // Configura a brisa e defini o ícone
    fboBrisa.begin();
    ofClear(0,0,0, 0);
    fboBrisa.end();

    converterGray = false;
}

void ImagemBrisa::update( float dt ) {

    fboBrisa.begin();
    ofClear(0,0,0, 0);

    // Distorce a imagem e aplica filtros
    if (img.isAllocated()) {
        ofPushMatrix();
        ofTranslate(WIDTH/2 + deslocX, HEIGHT/2 + deslocY, 0);
        img.setAnchorPercent(0.5, 0.5);
        if (rotacionaSozinho) {
            rotacao += ofNoise(ofGetElapsedTimef());
            if (rotacao > 360) rotacao = 0;
        }
        ofRotate(rotacao);

        ofScale(proporcao, proporcao, 1);

        if (converterGray) {
            imagemGray = img;
            imagemGray.brightnessContrast(brilhoBrisa, contrasteBrisa);
            imagemGray.setAnchorPercent(0.5, 0.5);
            imagemGray.draw(0, 0);
        }
        else {
            img.draw(0, 0);
        }
        ofPopMatrix();
    }

    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void ImagemBrisa::draw() {
    Brisa::draw();
}

void ImagemBrisa::drawControles(int iBrisa) {

    if (ImGui::Button("Carregar Imagem")) { 
        ImGui::OpenPopup("loadImagens");
    }
    if (ImGui::BeginPopup("loadImagens")) {
        listaImagens(); 
        ImGui::EndPopup();
    } 

    ImGui::Checkbox("Converter pra p&b", &converterGray);

    Brisa::drawControles(iBrisa);
}



void ImagemBrisa::listaImagens() {

    ofDirectory dirImg;
    //2. Carrega numero de pastas de sequencias
    int nImgs = dirImg.listDir("../data/img/imagens");

    //4. Abre pastas
    for (int i=0; i<nImgs; i++) {
        string video = dirImg.getPath( i );
        if (ImGui::Selectable(video.c_str())) {
            setupImagem(video);
        }

    }
}

void ImagemBrisa::setupImagem(string imgPath) {
    caminhoImagem = imgPath;
    if(caminhoImagem != "") {
        img.load(imgPath);
        img.setAnchorPercent(0.5, 0.5);
    }
}
