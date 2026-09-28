#include "Brisas/ColecaoBrisas.h"
#include "Brisas/kinect/FonteGlobalKinect.h"
#include "GuiApp.h"
#include "ofAppGLFWWindow.h"
#include <cassert>
#include <iostream>
#include <type_traits>

struct BrisaContada : Brisa
{
    explicit BrisaContada(int &destruicoes) : destruicoes(destruicoes) {}
    ~BrisaContada() override { ++destruicoes; }
    int &destruicoes;
};

void testaPropriedade()
{
    static_assert(!std::is_copy_constructible<Brisa>::value, "Camadas não podem ser copiadas");
    static_assert(!std::is_move_constructible<ColecaoBrisas>::value, "A coleção tem endereço estável");
    static_assert(!std::is_copy_constructible<FonteGlobalKinect>::value, "Fonte única");
    static_assert(std::is_same<decltype(FonteGlobalKinect::getInstance().getDepthPixels()),
                  const ofPixels &>::value, "Frames compartilhados são somente leitura");
    int destruicoes = 0;
    {
        ColecaoBrisas camadas;
        assert(camadas.adiciona(nullptr) == 0);
        assert(!camadas.remove(123));
        assert(!camadas.move(123, -1));
        const auto a = camadas.adiciona(std::make_unique<BrisaContada>(destruicoes));
        const auto b = camadas.adiciona(std::make_unique<BrisaContada>(destruicoes));
        camadas.busca(a)->brisaShader = b;
        auto *alvo = camadas.busca(b);
        assert(camadas.move(b, -1));
        assert(camadas.getBrisas().front()->getId() == b);
        assert(camadas.busca(camadas.busca(a)->brisaShader) == alvo);
        assert(camadas.move(b, -1)); // Primeiro vai ao fim.
        assert(camadas.getBrisas().back()->getId() == b);
        assert(camadas.remove(b));
        assert(destruicoes == 1);
        assert(camadas.busca(b) == nullptr);
        assert(camadas.busca(a)->brisaShader == 0);
        assert(!camadas.remove(b));
        assert(camadas.move(a, 1)); // Uma única camada.
        camadas.clear();
        camadas.clear();
        assert(destruicoes == 2);
        const auto c = camadas.adiciona(std::make_unique<BrisaContada>(destruicoes));
        assert(c > b);
        assert(camadas.busca(a) == nullptr);
    }
    assert(destruicoes == 3); // Destrutor da coleção também libera as camadas.
}

void testaGraficos(const std::string &demoRoot)
{
    ofGLFWWindowSettings settings;
    settings.setSize(320, 240);
    settings.visible = false;
    auto janela = ofCreateWindow(settings);
    assert(janela->getWindowContext() != nullptr);
    janela->makeCurrent();
    ofSetDataPathRoot("bin/data/");
    WindowManager::getInstance().setMainWindowSize(64, 48);
    auto &fonte = FonteGlobalKinect::getInstance();
    assert(&fonte == &FonteGlobalKinect::getInstance());
    assert(!fonte.getIsConnected());
    assert(!fonte.getDepthPixels().isAllocated());

    {
        ProcessamentoKinect depth(ProcessamentoKinect::Canal::Profundidade);
        ProcessamentoKinect rgb(ProcessamentoKinect::Canal::Rgb);
        ofPixels pixels;
        pixels.allocate(17, 13, OF_PIXELS_GRAY);
        pixels.set(100);
        depth.setBlur(2);
        assert(depth.processa(pixels));
        assert(depth.getPixels().size() == pixels.size());
        for (const auto pixel : pixels) assert(pixel == 100); // Entrada permanece intacta.
        pixels.allocate(11, 7, OF_PIXELS_RGB);
        pixels.set(80);
        assert(depth.processa(pixels)); // Mudança de dimensões e canais.
        assert(depth.getPixels().getNumChannels() == 1);
        assert(rgb.processa(pixels));
        assert(rgb.getPixels().getNumChannels() == 3);
        pixels.clear();
        assert(!depth.processa(pixels));
        assert(!depth.getPixels().isAllocated());

        std::vector<ImVec4> paleta;
        for (int repeticao = 0; repeticao < 8; ++repeticao)
        {
            ColecaoBrisas camadas;
            camadas.adiciona(std::make_unique<Colunas>());
            camadas.adiciona(std::make_unique<KinectContornosBrisa>());
            camadas.adiciona(std::make_unique<KinectCamBrisa>());
            camadas.adiciona(std::make_unique<SombraBrisa>(paleta));
            camadas.adiciona(std::make_unique<MatrizBrisa>(paleta));
            camadas.adiciona(std::make_unique<PoligonoBrisa>(paleta));
            for (const auto &brisa : camadas.getBrisas()) brisa->update(1.0f / 30);
            camadas.clear();
        }
    }

    if (!demoRoot.empty())
    {
        ofSetDataPathRoot(demoRoot);
        for (int tentativa = 0; tentativa < 100; ++tentativa)
        {
            fonte.update();
            if (fonte.getDepthPixels().isAllocated() && fonte.getRgbPixels().isAllocated()) break;
            ofSleepMillis(20);
        }
        assert(fonte.getDepthPixels().isAllocated());
        assert(fonte.getRgbPixels().isAllocated());
        const ofPixels depthAntes = fonte.getDepthPixels();
        const ofPixels rgbAntes = fonte.getRgbPixels();
        std::vector<ImVec4> paleta;
        ColecaoBrisas camadas;
        camadas.adiciona(std::make_unique<Colunas>());
        camadas.adiciona(std::make_unique<KinectContornosBrisa>());
        camadas.adiciona(std::make_unique<KinectCamBrisa>());
        camadas.adiciona(std::make_unique<SombraBrisa>(paleta));
        camadas.adiciona(std::make_unique<MatrizBrisa>(paleta));
        for (const auto &brisa : camadas.getBrisas())
        {
            brisa->update(1.0f / 30);
            brisa->draw();
        }
        assert(std::equal(depthAntes.begin(), depthAntes.end(), fonte.getDepthPixels().begin()));
        assert(std::equal(rgbAntes.begin(), rgbAntes.end(), fonte.getRgbPixels().begin()));
        // Reordena e exclui a fonte de um shader antes de desenhar novamente.
        const auto alvo = camadas.getBrisas().back()->getId();
        camadas.getBrisas().front()->brisaShader = alvo;
        camadas.move(alvo, -1);
        camadas.remove(alvo);
        for (const auto &brisa : camadas.getBrisas()) brisa->draw();
        camadas.clear();
        ofSetDataPathRoot("bin/data/");
        std::cout << "OK: RGB/profundidade compartilhados e preservados por todas as camadas\n";
    }

    // Exercita também ImGui e o encerramento explícito com contexto vivo.
    {
        GuiApp app;
        app.setup();
        ImGui::GetIO().IniFilename = nullptr;
        app.registraBrisa(std::make_unique<KinectCamBrisa>());
        app.draw();
        app.exit();
        app.exit();
        assert(app.getBrisas().empty());
    }
    fonte.update(); // Após shutdown não reabre câmera ou players.
    assert(!fonte.getIsConnected());
    assert(!fonte.getDepthTexture().isAllocated());
    assert(!fonte.getRgbPixels().isAllocated());
    janela->close();
}

int main(int argc, char **argv)
{
    testaPropriedade();
    std::cout << "OK: propriedade, exclusão, reordenação e IDs estáveis\n";
    if (argc > 1 && std::string(argv[1]) == "--gl")
    {
        testaGraficos(argc > 2 ? argv[2] : "");
        std::cout << "OK: filtros, frames vazios, criação/exclusão de camadas e encerramento GL\n";
    }
}
