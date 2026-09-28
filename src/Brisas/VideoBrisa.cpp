#include "Brisa.h"

VideoBrisa::VideoBrisa() {
    setup();
    fboBrisa.allocate(WIDTH, HEIGHT);
    fboBrisa.begin();
    ofClear(0,0,0, 0);
    fboBrisa.end();
    posVideo = 0.6;
}
 
void VideoBrisa::update( float dt ) {
    if( video.isLoaded() )  {
        posVideo = video.getPosition();
        video.update();
    }

    fboBrisa.begin();
    ofClear(0,0,0, 0);

    if( video.isLoaded() ) {
        video.draw(-(widthDraw-WIDTH)/2, -(heightDraw-HEIGHT)/2, widthDraw, heightDraw);
    }

    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void VideoBrisa::draw() {
    Brisa::draw();
}

void VideoBrisa::drawControles(int iBrisa) {
    if(caminhoVideo.size() > 15)
        ImGui::Text(caminhoVideo.substr(15).c_str());
    if (video.isLoaded()) {
        // play e pause
        if (video.isPaused()) {
            if (ImGui::Button("Play")) {
                video.setPaused(false);
            }
        }
        else {
            if (ImGui::Button("Pause")) {
                video.setPaused(true);
            }
        }
        // Timeline
        ImGui::SliderFloat("Tempo", &posVideo, 0.6, 1);
    }

    if (ImGui::Button("Carregar Vídeo")) { 
        ImGui::OpenPopup("loadVideo");
    }
    if (ImGui::BeginPopup("loadVideo")) {
        listaVideos(); 
        ImGui::EndPopup();
    }
    Brisa::drawControles(iBrisa);
}



void VideoBrisa::listaVideos() {

    ofDirectory dirVideosF;

    //2. Carrega numero de pastas de sequencias
    int nVideosFolder = dirVideosF.listDir("../data/videos/");

    cout << "\nPastas de videos: " << nVideosFolder;
    //4. Abre pastas
    for (int i=0; i<nVideosFolder; i++) {	
        string videoFolder = dirVideosF.getPath( i );

        cout << "\nVideo Folder: " << videoFolder;
        if (ImGui::CollapsingHeader(videoFolder.substr(15).c_str())) {


            //2. Carrega numero de pastas de sequencias
            ofDirectory dirVideos;
            int nVideos = dirVideos.listDir(videoFolder.c_str());
            //4. Abre pastas
            for (int i = 0; i < nVideos; i++) {
                string video = dirVideos.getPath(i);
                if (ImGui::Selectable(video.substr(15).c_str())) {
                    setupVideo(video);
                }

            }
        }

    }
}

void VideoBrisa::setupVideo(string videoPath) {
    caminhoVideo = videoPath;
    if(caminhoVideo != "") {
        video.load(caminhoVideo);
        cout << "\nVideo carregado: " << caminhoVideo;
        video.play();
        cout << "\nVideo played: " << caminhoVideo;
        video.setVolume(0);

        heightOrig = video.getHeight();
        widthOrig = video.getWidth();

        // Caso tenha dado algo errado no codec/video/etc
        if(heightOrig == 0 || widthOrig == 0) {
            cout << "\nVideo deu ruim:" << caminhoVideo;
            video.close();
            return;
        }

        // background-size: cover;
        float videoProp = (float)widthOrig/heightOrig;
        cout << "\nProporções calculadas: " << caminhoVideo;
        if( videoProp > 4./3. ) {
            heightDraw = HEIGHT;
            widthDraw = widthOrig * (heightDraw/heightOrig);
        } else {
            widthDraw = WIDTH;
            heightDraw = heightOrig * (widthDraw/widthOrig);
        }
        cout << "\nredimensionado: " << widthOrig << "x" << heightOrig << " => " 
            << widthDraw << "x" << heightDraw;
    }
}
