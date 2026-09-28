#include "FinderBrisa.h"

// Class of Video Grabber to get input from cameras 
FinderBrisa::FinderBrisa(vector<ImVec4> *coresPaleta) {
    setup();

    mirrorHorizontal = mirrorVertical = false;

    ofSetBackgroundAuto(false);
    
    // Define index como -1 (nenhum dispositivo selecionado)
    indexDevice = -1;
    
    // Configura HaarFinder para corpo humano
    finderBody.setup("data/xml/haarcascade_fullbody.xml");
    // Configura HaarFinder para rosto
    finderFace.setup("data/xml/haarcascade_frontalface_alt.xml");
    // Configura HaarFinder para sorriso
    finderSmile.setup("data/xml/haarcascade_smile.xml");
    // Configura HaarFinder para parte superior do corpo
    finderUpperBody.setup("data/xml/haarcascade_upperbody.xml");
}


void FinderBrisa::update( float dt ) {
    fboBrisa.begin();
    ofSetColor(255,255,255,255);
    ofNoFill();
    if (cam.isInitialized())
    {
        cam.update();
        cam.draw(0, 0);
    }
    // Set colors to draw just the contour of rectangle

    pixelsBrisa.setImageType(OF_IMAGE_COLOR);
    finderBody.findHaarObjects(pixelsBrisa);
    finderFace.findHaarObjects(pixelsBrisa);
    // finderSmile.findHaarObjects(pixelsBrisa);
    // finderUpperBody.findHaarObjects(pixelsBrisa);

    ofSetColor(255, 0, 0, 255);
    for (int i = 0; i < finderBody.blobs.size(); i++)
    {
        ofDrawRectangle(finderBody.blobs[i].boundingRect);
    }

    // ofSetColor(255, 255, 0, 255);
    // for (int i = 0; i < finderUpperBody.blobs.size(); i++)
    // {
    //     ofDrawRectangle(finderUpperBody.blobs[i].boundingRect);
    // }

    ofSetColor(0, 255, 0, 255);
    for (int i = 0; i < finderFace.blobs.size(); i++)
    {
        ofDrawRectangle(finderFace.blobs[i].boundingRect);
    }

    // ofSetColor(0, 0, 255, 255);
    // for (int i = 0; i < finderSmile.blobs.size(); i++)
    // {
    //     ofDrawRectangle(finderSmile.blobs[i].boundingRect);
    // }

    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void FinderBrisa::draw() {
    Brisa::draw();
}

void FinderBrisa::drawControles(int iBrisa) {
    if (ImGui::Button("Carregar Camera")) { 
        ImGui::OpenPopup("deviceIndex");
    }
    if (ImGui::BeginPopup("deviceIndex")) {
        if (ImGui::Selectable("0")) {
            indexDevice = 0;
            cam.setDeviceID(indexDevice);
            cam.setup(WIDTH, HEIGHT);
        }
        if (ImGui::Selectable("1")) {
            indexDevice = 1;
            cam.setDeviceID(indexDevice);
            cam.setup(WIDTH, HEIGHT);
        }
        if (ImGui::Selectable("2")) {
            indexDevice = 2;
            cam.setDeviceID(indexDevice);
            cam.setup(WIDTH, HEIGHT);
        }
        ImGui::EndPopup();
    } 
}
