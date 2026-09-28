
#include "Colunas.h"
#include "FonteGlobalKinect.h"

Colunas::Colunas()
{
  Brisa::setup();
  // purple
  cores.push_back(ImVec4(0.4, 0.2, 0.8, 1.0));
  // pink
  cores.push_back(ImVec4(0.9, 0.2, 1.0, 1.0));
  // black
  cores.push_back(ImVec4(0.0, 0.0, 0.0, 1.0));


}

void Colunas::drawColuna(int x, int y, float brilho)
{
  int altura = (brilho / 200) * alturaColuna;
  ofColor corPilar = cores[0];
  ofColor corQuadrado = cores[1];
  ofColor corBorda = cores[2];
  ofColor corPilarEscuro = corPilar;
  corPilarEscuro.setBrightness(100); // Escurece o lado do pilar
  glPushMatrix();

  // glTranslatef(x*gapX * (WIDTH / (float)width), y*gapY* (HEIGHT / (float)height) * 0.81 - altura, 0);
  glTranslatef(x, y - altura, 0);
  ofSetColor(corPilar);

  ofFill();
  ofDrawRectangle(-tamanhoColuna * 0.51, tamanhoColuna * 0.414, tamanhoColuna * 0.51, tamanhoColuna * 8);
  ofSetColor(0x000000);
  ofNoFill();
  ofDrawRectangle(-tamanhoColuna * 0.51, tamanhoColuna * 0.414, tamanhoColuna * 0.51, tamanhoColuna * 8);

  ofSetColor(corPilarEscuro);
  ofFill();
  ofDrawRectangle(0, tamanhoColuna * 0.414, tamanhoColuna * 0.51, tamanhoColuna * 8);
  ofSetColor(corBorda);
  ofNoFill();
  ofDrawRectangle(0, tamanhoColuna * 0.414, tamanhoColuna * 0.51, tamanhoColuna * 8);

  glScalef(1, 0.8, 1);
  glRotatef(45, 0, 0, 1);
  int angle = corQuadrado.getSaturation();
  corQuadrado.setSaturation(angle + altura / 3);
  ofSetColor(corQuadrado);
  ofFill();
  ofDrawRectangle(0, 0, tamanhoColuna * 0.72, tamanhoColuna * 0.72);
  ofSetColor(corBorda);
  ofNoFill();
  ofDrawRectangle(0, 0, tamanhoColuna * 0.72, tamanhoColuna * 0.72);

  glPopMatrix();
}

void Colunas::update(float dt)
{
  float currentFactor = 0.7; // Fator do frame atual (para visibilidade)

  Brisa::update(dt);

  const ofPixels &compartilhados = FonteGlobalKinect::getInstance().getDepthPixels();
  if (!compartilhados.isAllocated())
  {
    blendedFrameMat.release();
    limpaFrame();
    return;
  }
  // OpenCV recebe uma cópia local; o frame compartilhado permanece imutável.
  ofPixels depthPixels = compartilhados;
  if (!colunasFbo.isAllocated() || colunasFbo.getWidth() != depthPixels.getWidth()
      || colunasFbo.getHeight() != depthPixels.getHeight())
      colunasFbo.allocate(depthPixels.getWidth(), depthPixels.getHeight(), GL_RGBA);
  cv::Mat cvMatImage(cv::Size(depthPixels.getWidth(), depthPixels.getHeight()), CV_8UC1, depthPixels.getData());
  cv::Mat cvBlurredImage;

  // Aplique o desfoque
  cv::blur(cvMatImage, cvBlurredImage, cv::Size(blurSize, blurSize));

  // Inicialize blendedFrameMat com as mesmas dimensões e tipo que cvBlurredImage, se ainda não estiver configurado
  if (blendedFrameMat.empty() || blendedFrameMat.size() != cvBlurredImage.size())
  {
    blendedFrameMat = cv::Mat::zeros(cvBlurredImage.size(), cvBlurredImage.type());
  }

  // Aplicar o efeito de rastro: blendedFrame = blendedFrame * trailFactor + currentFrame * currentFactor
  cv::addWeighted(cvBlurredImage, currentFactor, blendedFrameMat, trailFactor, 0, blendedFrameMat);

  // Atualize o ofImage com o resultado misturado (blended)
  blendedImage.setFromPixels(blendedFrameMat.data, blendedFrameMat.cols, blendedFrameMat.rows, OF_IMAGE_GRAYSCALE); // Mantenha OF_IMAGE_GRAYSCALE para 1 canal
  blendedImage.update();

  colunasFbo.begin();
  ofClear(0, 0, 0, 0);
  ofEnableSmoothing();
  ofSetColor(255, 255, 255, 255);
  ofFill();
  // Desenhe as colunas
  // TODO: Inserir colunas de "borda" em cima e embaixo
  for (int y = 0; y < blendedImage.getHeight(); y += tamanhoColuna * 0.84)
  {
    for (int x = 0; x < blendedImage.getWidth(); x += tamanhoColuna)
    {
      float brightness = blendedImage.getColor(x, y).getBrightness();
      glEnable(GL_LINE_SMOOTH);
      drawColuna(x, y, brightness);
      glDisable(GL_LINE_SMOOTH);
    }
    // get 2 row to fill the gap
    for (int x = tamanhoColuna / 2; x < blendedImage.getWidth(); x += tamanhoColuna)
    {
      int linha = std::min(static_cast<int>(blendedImage.getHeight()) - 1,
                           static_cast<int>(y + tamanhoColuna * 0.42));
      float brightness = blendedImage.getColor(x, linha).getBrightness();
      glEnable(GL_LINE_SMOOTH);
      drawColuna(x, y + tamanhoColuna * 0.42, brightness);
      glDisable(GL_LINE_SMOOTH);
    }
  }
  ofDisableSmoothing();
  colunasFbo.end();
  fboBrisa.begin();
  ofClear(255, 255, 255, 0);
  ofSetColor(255, 255, 255, 255);
  colunasFbo.draw(0, 0, fboBrisa.getWidth(), fboBrisa.getHeight());
  fboBrisa.end();
  fboBrisa.readToPixels(pixelsBrisa);
}

void Colunas::draw()
{
  Brisa::draw();
}

void Colunas::drawControles(int iBrisa)
{
  if (ImGui::CollapsingHeader("Cores"))
  {
    ImGui::ColorEdit4("Cor 1", (float *)&cores[0]);
    ImGui::ColorEdit4("Cor 2", (float *)&cores[1]);
    ImGui::ColorEdit4("Cor 3", (float *)&cores[2]);
  }
  if (ImGui::CollapsingHeader("Configurações"))
  {
    ImGui::SliderInt("Tamanho da coluna", &tamanhoColuna, 5, 100);
    ImGui::SliderFloat("Altura da coluna", &alturaColuna, 1, 100);
    ImGui::SliderInt("Desfoque", &blurSize, 1, 100);
    ImGui::SliderFloat("trailFactor", &trailFactor, 0.0, 1.0);
  }
}
