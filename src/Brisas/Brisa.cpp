#include "Brisa.h"
#include "ColecaoBrisas.h"

void Brisa::setup()
{
    // btnConfigBrisa = gui->loadTexture(iconPath);fboBrisa.clear();

    int width = WindowManager::getInstance().getMainWindowWidth();
    int height = WindowManager::getInstance().getMainWindowHeight();

    pixelsBrisa.allocate(width, height, OF_IMAGE_COLOR);
    pixelsBrisa.set(0);
    fboBrisa.allocate(width, height, GL_RGBA);
    fboBrisa.begin();
    ofClear(0, 0, 0, 0);
    fboBrisa.end();

    // variaveis do shader
    ligaShader = false;

    clearFrames = true;

    brisaShader = 0;

    opacidade = 0;
    // Inicializa variaveis de distorções
    brilhoBrisa = contrasteBrisa = 0.5;
    deslocX = deslocY = 0;
    rotacao = 0;
    proporcao = 1;
    rotacionaSozinho = false;
}

void Brisa::limpaFrame()
{
    fboBrisa.begin();
    ofClear(0, 0, 0, 0);
    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void Brisa::update(float dt)
{
}

void Brisa::mostraBrisas()
{
}

void Brisa::draw()
{
    int width = WindowManager::getInstance().getMainWindowWidth();
    int height = WindowManager::getInstance().getMainWindowHeight();

    ofSetColor(255, 255, 255, opacidade);
    if (ligaShader)
    {
        shaderBrisa.begin();
        const Brisa *externa = colecao ? colecao->busca(brisaShader) : nullptr;
        // Sem seleção, usa a própria textura e substitui qualquer binding anterior.
        const ofTexture &textura = externa && externa->fboBrisa.isAllocated()
            ? externa->fboBrisa.getTexture() : fboBrisa.getTexture();
        shaderBrisa.setUniformTexture("texturaBrisaExterna", textura, 1);
        shaderBrisa.setUniformTexture("texture0", fboBrisa.getTextureReference(), 0);
        shaderBrisa.setUniform2f("resolution", fboBrisa.getWidth(), fboBrisa.getHeight());
    }

    ofPushMatrix();
    ofTranslate(width / 2 + deslocX, height / 2 + deslocY, 0);
    fboBrisa.setAnchorPercent(0.5, 0.5);
    if (rotacionaSozinho)
    {
        rotacao += ofNoise(ofGetElapsedTimef());
        if (rotacao > 360)
            rotacao = 0;
    }
    ofRotate(rotacao);

    ofScale(proporcao, proporcao, 1);
    fboBrisa.draw(0, 0);
    ofPopMatrix();

    if (ligaShader)
    {
        shaderBrisa.end();
    }
}

void Brisa::drawControles(int iBrisa)
{
    desenharControlesPosicao();
}

ofRectangle Brisa::limitesMiniatura(int i)
{
    return ofRectangle(0, i * (alturaMiniatura + bordaMiniatura * 2),
                       larguraMiniatura + bordaMiniatura * 2,
                       alturaMiniatura + bordaMiniatura * 2);
}

void Brisa::desenhaMiniatura(int i, bool ativa)
{
    const ofRectangle limites = limitesMiniatura(i);
    const float x = limites.x + bordaMiniatura;
    const float y = limites.y + bordaMiniatura;
    ofPushStyle();
    ofFill();
    imgBtn.setFromPixels(pixelsBrisa);
    if (ativa)
    {
        ofSetColor(205, 100, 100);
    }
    else
    {
        ofSetColor(30);
    }
    ofDrawRectangle(limites);
    ofSetColor(0);
    ofDrawRectangle(x, y, larguraMiniatura, alturaMiniatura);
    ofSetColor(255);
    imgBtn.draw(x, y, larguraMiniatura, alturaMiniatura);
    ofPopStyle();
}

AcaoBrisa Brisa::desenhaJanela(int i)
{

    std::ostringstream oss;
    std::string text = "Brisa ";
    text += std::to_string(i);

    ImGui::SetNextWindowSize(ofVec2f(500, 548), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ofVec2f(210, 430), ImGuiCond_FirstUseEver);
    string titulo = "Brisa #" + to_string(i + 1) + "###brisa-" + to_string(id);
    AcaoBrisa acao = AcaoBrisa::Nenhuma;
    ImGuiWindowFlags window_flags = 0;
    //  window_flags |= ImGuiWindowFlags_NoResize;
    window_flags |= ImGuiWindowFlags_NoCollapse;
    //  window_flags |= ImGuiWindowFlags_NoMove;
    bool janelaAberta = true;
    ImGui::Begin(titulo.c_str(), &janelaAberta, window_flags);

    ImGui::SliderInt("Opacidade", &opacidade, 0, 255);
    drawControles(i);

    if (ImGui::Button("Trazer pra frente"))
    {
        acao = AcaoBrisa::Frente;
    }
    ImGui::SameLine();
    if (ImGui::Button("Esconder pra trás"))
    {
        acao = AcaoBrisa::Tras;
    }
    desenharControlesShader();
    if (ImGui::Button("Excluir Brisa")) acao = AcaoBrisa::Excluir;
    ImGui::End();
    return acao;
}

void Brisa::loadShader(string shader)
{
    fragShaderPath = shader;
    shaderBrisa.load("../data/shaders/vertexdummy.c", fragShaderPath);
}

void Brisa::listaBrisas()
{

    if (ImGui::Selectable("Nenhuma")) brisaShader = 0;
    if (!colecao) return;
    int i = 0;
    for (const auto &brisa : colecao->getBrisas())
    {
        string nome = "Brisa #" + to_string(++i);
        if (ImGui::Selectable(nome.c_str(), brisaShader == brisa->getId()))
            brisaShader = brisa->getId();
    }
}

void Brisa::desenharControlesShader()
{
    if (ImGui::CollapsingHeader("Shader"))
    {
        if (shaderBrisa.isLoaded())
        {
            ImGui::Text(fragShaderPath.substr(23).c_str());
            ImGui::Checkbox("Ligar Shader", &ligaShader);
        }
        if (ImGui::Button("Carregar Shader"))
        {
            ImGui::OpenPopup("loadShader");
        }
        if (ImGui::BeginPopup("loadShader"))
        {
            listaShaders();
            ImGui::EndPopup();
        }
        string numBrisa = "Brisa Shader: nenhuma";
        if (colecao)
        {
            int i = 0;
            for (const auto &brisa : colecao->getBrisas())
            {
                ++i;
                if (brisa->getId() == brisaShader)
                    numBrisa = "Brisa Shader: #" + to_string(i);
            }
        }
        if (ImGui::Button(numBrisa.c_str()))
        {
            ImGui::OpenPopup("selectBrisa");
        }
        if (ImGui::BeginPopup("selectBrisa"))
        {
            listaBrisas();
            ImGui::EndPopup();
        }
    }
}

void Brisa::listaShaders()
{
    if (ImGui::CollapsingHeader("Alpha"))
    {
        ofDirectory dirShaders;
        // 2. Carrega numero de pastas de sequencias
        int nShaders = dirShaders.listDir("../data/shaders/alpha");

        // 4. Abre pastas
        for (int i = 0; i < nShaders; i++)
        {
            string shader = dirShaders.getPath(i);
            if (ImGui::Selectable(shader.substr(22).c_str()))
            {
                loadShader(shader);
            }
        }
    }
    if (ImGui::CollapsingHeader("Mix Brisa"))
    {
        ofDirectory dirShaders;
        // 2. Carrega numero de pastas de sequencias
        int nShaders = dirShaders.listDir("../data/shaders/mixbrisa");

        // 4. Abre pastas
        for (int i = 0; i < nShaders; i++)
        {
            string shader = dirShaders.getPath(i);
            if (ImGui::Selectable(shader.substr(25).c_str()))
            {
                loadShader(shader);
            }
        }
    }
    if (ImGui::CollapsingHeader("Outros"))
    {
        ofDirectory dirShaders;
        // 2. Carrega numero de pastas de sequencias
        int nShaders = dirShaders.listDir("../data/shaders/outros");

        // 4. Abre pastas
        for (int i = 0; i < nShaders; i++)
        {
            string shader = dirShaders.getPath(i);
            if (ImGui::Selectable(shader.substr(23).c_str()))
            {
                loadShader(shader);
            }
        }
    }
}

void Brisa::desenharControlesPosicao()
{
    if (ImGui::CollapsingHeader("Posição e Tamanho"))
    {
        int width = WindowManager::getInstance().getMainWindowWidth();
        int height = WindowManager::getInstance().getMainWindowHeight();

        // ImGui::SliderFloat("brilho", &brilhoBrisa, 0, 1);
        // ImGui::SliderFloat("contraste", &contrasteBrisa, 0, 1);
        ImGui::SliderInt("X", &deslocX, -200, 200);
        ImGui::SliderInt("Y", &deslocY, -200, 200);
        ImGui::SliderFloat("Proporcao", &proporcao, 0.2, 3, "%.2f", 2);
        ImGui::SliderFloat("Rotação", &rotacao, -180, 180);
        ImGui::SameLine();
        ImGui::Checkbox("Automático", &rotacionaSozinho);
    }
}
