#include "Brisa.h"

GifBrisa::GifBrisa() {
    setup();
    fboBrisa.allocate(WIDTH, HEIGHT);
    fboBrisa.begin();
    ofClear(0,0,0, 0);
    fboBrisa.end();

  rotacao = 0;
  proporcao = 1;
  deslocX = deslocY = 0;
}

void GifBrisa::update(float dt)
{
  if (video.isLoaded())
  {
    video.update();
  }

  fboBrisa.begin();
  ofClear(0, 0, 0, 0);

  if (video.isLoaded())
  {
    ofPushMatrix();
    ofTranslate(WIDTH / 2 + deslocX, HEIGHT / 2 + deslocY, 0);
    video.setAnchorPercent(0.5, 0.5);
    ofRotate(rotacao);

    ofScale(proporcao, proporcao, 1);
    video.draw(-(widthDraw - WIDTH) / 2, -(heightDraw - HEIGHT) / 2 + 100, widthDraw, heightDraw);
    ofPopMatrix();
  }

  fboBrisa.end();
  fboBrisa.readToPixels(pixelsBrisa);
}

void GifBrisa::draw() {
    Brisa::draw();
}

void GifBrisa::drawControles(int iBrisa)
{

  if (ImGui::Button("Carregar Gif"))
  {
    ImGui::OpenPopup("loadVideo");
  }
  if (ImGui::BeginPopup("loadVideo"))
  {
    listaGifs();
    ImGui::EndPopup();
  }

  ImGui::SliderInt("desloca X", &deslocX, -600, 600);
  ImGui::SliderInt("desloca Y", &deslocY, -600, 600);
  ImGui::SliderFloat("Proporcao", &proporcao, 0.2, 4);
  ImGui::SliderFloat("Rotação", &rotacao, -180, 180);

}

void GifBrisa::listaGifs()
{

  ofDirectory dirVideosF;

  // 2. Carrega numero de pastas de sequencias
  int nVideosFolder = dirVideosF.listDir("../data/gifs/");

  // 4. Abre pastas
  for (int i = 0; i < nVideosFolder; i++)
  {
    string videoFolder = dirVideosF.getPath(i);

    if (ImGui::CollapsingHeader(videoFolder.substr(14).c_str()))
    {

      // 2. Carrega numero de pastas de sequencias
      ofDirectory dirVideos;
      int nVideos = dirVideos.listDir(videoFolder.c_str());
      // 4. Abre pastas
      for (int i = 0; i < nVideos; i++)
      {
        string video = dirVideos.getPath(i);
        if (ImGui::Selectable(video.substr(14).c_str()))
        {
          setupVideo(video);
        }
      }
    }
  }
}

void GifBrisa::setupVideo(string videoPath)
{
  caminhoVideo = videoPath;
  if (caminhoVideo != "")
  {
    video.load(caminhoVideo);
    cout << "\nVideo loaded: " << caminhoVideo;
    video.play();
    cout << "\nVideo played: " << caminhoVideo;
    video.setVolume(0);

    heightOrig = video.getHeight();
    widthOrig = video.getWidth();

    // Caso tenha dado algo errado no codec/video/etc
    if (heightOrig == 0 || widthOrig == 0)
    {
      video.close();
      return;
    }

    // background-size: cover;
    float videoProp = (float)widthOrig / heightOrig;
    cout << "\nProporções calculadas: " << caminhoVideo;
    if (videoProp >= 4. / 3.)
    {
      heightDraw = HEIGHT;
      widthDraw = widthOrig * (heightDraw / heightOrig);
    }
    else
    {
      widthDraw = WIDTH;
      heightDraw = heightOrig * (widthDraw / widthOrig);
    }
    cout << "\nredimensionado: " << widthOrig << "x" << heightOrig << " => "
         << widthDraw << "x" << heightDraw;
  }
}
