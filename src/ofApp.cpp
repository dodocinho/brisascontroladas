#include "ofApp.h"
#include <algorithm>
#include <cmath>

//--------------------------------------------------------------
void ofApp::setup()
{
    ofSetFrameRate(30);
    WindowManager::getInstance().setMainWindowSize(ofGetWidth(), ofGetHeight());

    cout << "OpenGL Version: " << glGetString(GL_VERSION) << endl;
    cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << endl;
}

//--------------------------------------------------------------
void ofApp::update()
{

    switch (gui->iBlend)
    {
    case 1:
        gui->blendMode = OF_BLENDMODE_ALPHA;
        break;
    case 2:
        gui->blendMode = OF_BLENDMODE_ADD;
        break;
    case 3:
        gui->blendMode = OF_BLENDMODE_SCREEN;
        break;
    case 4:
        gui->blendMode = OF_BLENDMODE_MULTIPLY;
        break;
    case 5:
        gui->blendMode = OF_BLENDMODE_SUBTRACT;
        break;
    default:
        gui->blendMode = OF_BLENDMODE_ALPHA;
    }

    ofEnableBlendMode(gui->blendMode);

    if (gui->isRecording)
    {
        saveFrame();
    }
}

//--------------------------------------------------------------
void ofApp::draw()
{
    gui->transmissor.atualizar(TransmissorSrt::larguraEnvio, TransmissorSrt::alturaEnvio);

    ofBackground(0, 0, 0);
    ofEnableAntiAliasing();
    // Consulta a lista atual: a interface pode excluir camadas entre frames.
    const auto &brisasAtivas = gui->getBrisas();
    for (int i = static_cast<int>(brisasAtivas.size()) - 1; i >= 0; i--)
    {
        brisasAtivas[i]->draw();
    }
    ofEnableAntiAliasing();

    if (gui->transmissor.aceitaQuadro()) capturarEnvio();
}

void ofApp::capturarEnvio()
{
    const int origemW = ofGetWidth();
    const int origemH = ofGetHeight();
    const int destinoW = TransmissorSrt::larguraEnvio;
    const int destinoH = TransmissorSrt::alturaEnvio;
    if (!gl || origemW < 2 || origemH < 2) return;

    // A janela tem multisampling. Copiar e reduzir no mesmo passo desloca cada
    // linha: o passo da linha deixa de ser largura * 4 bytes e a imagem vira escada.
    gl->saveScreen(0, 0, origemW, origemH, quadroCheio);
    if (quadroCheio.getNumChannels() != 4 ||
        quadroCheio.getWidth() != static_cast<std::size_t>(origemW) ||
        quadroCheio.getHeight() != static_cast<std::size_t>(origemH))
    {
        return;
    }

    quadroTransmissao.allocate(destinoW, destinoH, OF_PIXELS_BGRA);
    const unsigned char *origem = quadroCheio.getData();
    unsigned char *destino = quadroTransmissao.getData();
    constexpr int canais = 4;

    for (int y = 0; y < destinoH; ++y)
    {
        const float gy = (static_cast<float>(y) + 0.5f) * static_cast<float>(origemH) / static_cast<float>(destinoH) - 0.5f;
        const int y0 = std::max(0, std::min(origemH - 1, static_cast<int>(std::floor(gy))));
        const int y1 = std::min(origemH - 1, y0 + 1);
        const float fy = std::max(0.f, std::min(1.f, gy - static_cast<float>(y0)));
        const unsigned char *linha0 = origem + (static_cast<std::size_t>(y0) * origemW) * canais;
        const unsigned char *linha1 = origem + (static_cast<std::size_t>(y1) * origemW) * canais;

        for (int x = 0; x < destinoW; ++x)
        {
            const float gx = (static_cast<float>(x) + 0.5f) * static_cast<float>(origemW) / static_cast<float>(destinoW) - 0.5f;
            const int x0 = std::max(0, std::min(origemW - 1, static_cast<int>(std::floor(gx))));
            const int x1 = std::min(origemW - 1, x0 + 1);
            const float fx = std::max(0.f, std::min(1.f, gx - static_cast<float>(x0)));
            const unsigned char *p00 = linha0 + static_cast<std::size_t>(x0) * canais;
            const unsigned char *p10 = linha0 + static_cast<std::size_t>(x1) * canais;
            const unsigned char *p01 = linha1 + static_cast<std::size_t>(x0) * canais;
            const unsigned char *p11 = linha1 + static_cast<std::size_t>(x1) * canais;
            unsigned char *saida = destino + (static_cast<std::size_t>(y) * destinoW + x) * canais;
            for (int canal = 0; canal < canais; ++canal)
            {
                const float acima = p00[canal] + (static_cast<float>(p10[canal]) - p00[canal]) * fx;
                const float abaixo = p01[canal] + (static_cast<float>(p11[canal]) - p01[canal]) * fx;
                saida[canal] = static_cast<unsigned char>(acima + (abaixo - acima) * fy + 0.5f);
            }
        }
    }

    gui->transmissor.enviar(quadroTransmissao);
}

//--------------------------------------------------------------
void ofApp::exit()
{
    if (gui) gui->transmissor.parar();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key)
{
}

//--------------------------------------------------------------
void ofApp::keyReleased(int key)
{
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y)
{
}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button)
{
}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y)
{
}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y)
{
}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h)
{
    WindowManager::getInstance().setMainWindowSize(w, h);
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg)
{
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo)
{
}

void ofApp::saveFrame()
{
    ofPixels pixels;
    ofImage screenImage;
    // get current hour and minute
    time_t t = time(0);
    struct tm *now = localtime(&t);
    int hour = now->tm_hour;
    int min = now->tm_min;

    screenImage.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
    screenImage.save("recording/" + ofToString(hour) + "_" + ofToString(min) + "_" + "frame_" + ofToString(frameCount) + ".png");
    gui->setFrameCountRecorded(frameCount);
    frameCount++;
}
