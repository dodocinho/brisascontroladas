#pragma once

// [agente de IA] Tamanho vivo das duas janelas. Brisa::setup e Brisa::draw
// já consultam isto. GifBrisa e as constantes WIDTH/HEIGHT em Brisa.h
// ainda desenham como se a projeção fosse sempre 1024x768.
class WindowManager {
public:
    static WindowManager& getInstance() {
        static WindowManager instance; // garantido como sendo destruído
        return instance;
    }

    void setMainWindowSize(int width, int height) {
        mainWindowWidth = width;
        mainWindowHeight = height;
    }

    void setGuiWindowSize(int width, int height) {
        guiWindowWidth = width;
        guiWindowHeight = height;
    }

    int getMainWindowWidth() { return mainWindowWidth; }
    int getMainWindowHeight() { return mainWindowHeight; }
    int getGuiWindowWidth() { return guiWindowWidth; }
    int getGuiWindowHeight() { return guiWindowHeight; }

private:
    WindowManager() {} // Construtor privado
    int mainWindowWidth = 1024;
    int mainWindowHeight = 768;
    int guiWindowWidth = 1500;
    int guiWindowHeight = 800;
};