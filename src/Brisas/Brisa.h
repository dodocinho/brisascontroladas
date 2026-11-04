#ifndef BRISA_H__
#define BRISA_H__

const float WIDTH = 1024;
const float HEIGHT = 768;

#include "ofxImGui.h"
// #include "ofxTensorFlow2.h"
#include "ofxOpenCv.h"
#include <cstdint>
#include "ofxOsc.h"
#include "../WindowManager.h"

using BrisaId = std::uint64_t;
class ColecaoBrisas;
enum class AcaoBrisa { Nenhuma, Excluir, Frente, Tras };

// GuiApp atualiza as camadas e desenha os controles; ofApp desenha a projeção.
class Brisa
{
public:
    Brisa() = default;
    virtual ~Brisa() = default;
    Brisa(const Brisa &) = delete;
    Brisa &operator=(const Brisa &) = delete;
    BrisaId getId() const { return id; }

    string iconPath;
    GLuint btnConfigBrisa; // Botão pra mostrar as configs
    ofFbo fboBrisa;
    GLuint pixelsBtn;
    ofPixels pixelsBrisa;
    void mostraBrisas();
    ofImage imgBtn;

    ofShader shaderBrisa;
    string fragShaderPath;
    BrisaId brisaShader = 0;
    bool ligaShader;
    bool clearFrames;

    virtual void setup();          // Configura brisa
    virtual void update(float dt); // Recalcula brisa
    virtual void draw();
    void limpaFrame();

    virtual void drawControles(int iBrisa);
    virtual void desenhaMiniatura(int i, bool focada);
    // Textura de ajuste (Kinect filtrado). Nulo desenha a própria brisa ampliada.
    virtual const ofTexture *previaAmpliada() const { return nullptr; }
    static constexpr int larguraMiniatura = 160;
    static constexpr int alturaMiniatura = 120;
    static constexpr int bordaMiniatura = 2;
    static ofRectangle limitesMiniatura(int i);
    AcaoBrisa desenhaJanela(int i);

    void listaShaders();
    void loadShader(string shader);
    void listaBrisas();
    void desenharControlesShader();

    float brilhoBrisa, contrasteBrisa;
    bool rotacionaSozinho;
    float proporcao, rotacao;
    int deslocX, deslocY;
    int opacidade;

    // Posição e tamanho da brisa
    void desenharControlesPosicao();
    int fboWidth, fboHeight;
    int fboX, fboY;

private:
    friend class ColecaoBrisas;
    BrisaId id = 0;
    const ColecaoBrisas *colecao = nullptr; // Válida por toda a vida desta camada.
};

class VideoBrisa : public Brisa
{
    float heightDraw, widthDraw, heightOrig, widthOrig;

public:
    VideoBrisa();
    void draw() override;
    void update(float dt);

    ofVideoPlayer video;
    string caminhoVideo;
    ofFbo fboKinect;

    float posVideo;

    void drawControles(int iBrisa) override;

    void listaVideos();
    void setupVideo(string videoPath);
};

class GifBrisa : public Brisa
{
    float heightDraw, widthDraw, heightOrig, widthOrig;

public:
    GifBrisa();
    void draw() override;
    void update(float dt);

    ofVideoPlayer video;
    string caminhoVideo;
    // TODO: Unificar com os membros da base. Os sliders locais usam estas cópias,
    // mas Brisa::draw usa a transformação da base.
    float proporcao, rotacao;
    int deslocX, deslocY;

    void drawControles(int iBrisa) override;

    void listaGifs();
    void setupVideo(string videoPath);
};

class ImagemBrisa : public Brisa
{
public:
    ImagemBrisa();
    void draw() override;
    void update(float dt);

    ofImage img;
    string caminhoImagem;

    void drawControles(int iBrisa) override;

    void listaImagens();
    void setupImagem(string imgPath);

    bool converterGray;
    ofxCvGrayscaleImage imagemGray;
};

class PoligonoBrisa : public Brisa
{
public:
    PoligonoBrisa(const vector<ImVec4> &coresPaleta);
    void draw() override;
    void update(float dt);

    void drawControles(int iBrisa) override;
    void desenhaPoligono(int radius);
    ImVec4 corBrisa, corComplementar;
    int vertices;
    int quantidade;
    int distancia;
    bool preencher;
    bool trocaVertices;
    bool rotacionar;
};

#endif
