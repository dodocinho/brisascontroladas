#include "CameraBrisa.h"

// Class of Video Grabber to get input from cameras
CameraBrisa::CameraBrisa()
{
    setup();

    mirrorHorizontal = mirrorVertical = false;

    ofSetBackgroundAuto(false);

    // Define index como -1 (nenhum dispositivo selecionado)
    indexDevice = -1;
    camX = camY = 0;
}

void CameraBrisa::update(float dt)
{
    fboBrisa.begin();
    ofSetColor(255, 255, 255, 255);
    if (cam.isInitialized())
    {
        cam.update();
        if (cam.isFrameNew())
        {
            ofClear(0, 0, 0, 0);
            cam.draw(camX, camY, camWidth, camHeight);
        }
    }
    fboBrisa.end();
    fboBrisa.readToPixels(pixelsBrisa);
}

void CameraBrisa::draw()
{
    Brisa::draw();
}

void CameraBrisa::drawControles(int iBrisa)
{
    int width = WindowManager::getInstance().getMainWindowWidth();
    int height = WindowManager::getInstance().getMainWindowHeight();
    if (ImGui::Button("Carregar Camera"))
    {
        ImGui::OpenPopup("deviceIndex");
    }
    if (ImGui::BeginPopup("deviceIndex"))
    {
        // Lista as cameras disponíveis
        vector devices = cam.listDevices();

        for (int i = 0; i < devices.size(); i++)
        {
            if (ImGui::Selectable(devices[i].deviceName.c_str()))
            {
                indexDevice = i;
                cam.setDeviceID(indexDevice);
                cam.setup(width, height);
                camOriginalHeight = cam.getHeight();
                camOriginalWidth = cam.getWidth();
                camWidth = camOriginalWidth;
                camHeight = camOriginalHeight;
            }
        }
        ImGui::EndPopup();
    }

    if (ImGui::CollapsingHeader("Cam: Redimensionar"))
    {
        // Mostra o ratio da camera quando ela está carregada, assim como a largura e altura originais
        if (cam.isInitialized())
        {
            ImGui::Text("Resolução: %dx%d", camOriginalWidth, camOriginalHeight);
            ImGui::Text("Ratio: %.2f", (float)camOriginalWidth / camOriginalHeight);
        }

        // Controle de tamanho da camera
        ImGui::SliderInt("Largura", &camWidth, 0, width * 2);
        ImGui::SliderInt("Altura", &camHeight, 0, height * 2);

        // Controle de posição da camera
        ImGui::SliderInt("X", &camX, -width, width);
        ImGui::SliderInt("Y", &camY, -height, height);

        // TODO: fbo não tem mirror
        // Controle de espelhamento
        // ImGui::Checkbox("Espelhamento Horizontal", &mirrorHorizontal);
        // ImGui::Checkbox("Espelhamento Vertical", &mirrorVertical);

        // Botão para redimensionar e centralizar a camera, estilo "cover" do CSS
        if (ImGui::Button("Redimensionar Cover"))
        {
            float ratio = (float)camOriginalWidth / camOriginalHeight;
            float ratioCam = (float)camWidth / camHeight;

            if (ratioCam > ratio)
            {
                camWidth = width;
                camHeight = camWidth / ratio;
                camX = 0;
                camY = (height - camHeight) / 2;
            }
            else
            {
                camHeight = height;
                camWidth = camHeight * ratio;
                camY = 0;
                camX = (width - camWidth) / 2;
            }
        }

        // Botão para redimensionar e centralizar a camera, estilo "contain" do CSS
        if (ImGui::Button("Redimensionar Contain"))
        {
            float ratio = (float)camOriginalWidth / camOriginalHeight;
            float ratioCam = (float)camWidth / camHeight;

            if (ratioCam > ratio)
            {
                camHeight = height;
                camWidth = camHeight * ratio;
                camY = 0;
                camX = (width - camWidth) / 2;
            }
            else
            {
                camWidth = width;
                camHeight = camWidth / ratio;
                camX = 0;
                camY = (height - camHeight) / 2;
            }
        }

        // Botão para centralizar a camera
        if (ImGui::Button("Centralizar"))
        {
            camX = (width - camWidth) / 2;
            camY = (height - camHeight) / 2;
        }
    }
    Brisa::drawControles(iBrisa);
}
