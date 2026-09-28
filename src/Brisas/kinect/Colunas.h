#include "../Brisa.h"

class Colunas : public Brisa
{

public:
  Colunas();
  void draw();
  void update(float dt);

  void drawControles(int iBrisa);

  int blurSize = 50; // Tamanho do desfoque aplicado à imagem de profundidade

  int tamanhoColuna = 32; // Intervalo entre as colunas

  vector<ImVec4> cores;
  cv::Mat blendedFrameMat; // Frame com o rastro
  ofImage blendedImage;
  ofFbo colunasFbo;

  float trailFactor = 0.9; // Fator de rastro
  float alturaColuna = 10; // Multiplicador de altura da coluna

  void drawColuna(int x, int y, float brilho);
};