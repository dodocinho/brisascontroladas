#include "ProcessamentoKinect.h"
#include "FonteGlobalKinect.h"
#include "ofxImGui.h"
#include <algorithm>

bool ProcessamentoKinect::update()
{
    const auto &fonte = FonteGlobalKinect::getInstance();
    const auto &pixels = canal == Canal::Rgb ? fonte.getRgbPixels() : fonte.getDepthPixels();
    return processa(pixels);
}

bool ProcessamentoKinect::processa(const ofPixels &pixels)
{
    if (!pixels.isAllocated())
    {
        resultado.clear();
        acumulado.release();
        textura.clear();
        return false;
    }
    // Cópia local: filtros de uma brisa nunca alteram o frame das outras.
    resultado = pixels;
    resultado.setImageType(canal == Canal::Rgb ? OF_IMAGE_COLOR : OF_IMAGE_GRAYSCALE);
    const int tipo = CV_MAKETYPE(CV_8U, resultado.getNumChannels());
    cv::Mat imagem(resultado.getHeight(), resultado.getWidth(), tipo, resultado.getData());
    for (int i = 0; i < erode; ++i) cv::erode(imagem, imagem, cv::Mat());
    for (int i = 0; i < dilate; ++i) cv::dilate(imagem, imagem, cv::Mat());
    for (int i = 0; i < blur; ++i) cv::GaussianBlur(imagem, imagem, cv::Size(5, 5), 0);
    for (int i = 0; i < blurForte; ++i) cv::GaussianBlur(imagem, imagem, cv::Size(15, 15), 0);

    cv::Mat atual;
    imagem.convertTo(atual, CV_32F);
    if (acumulado.empty() || acumulado.size() != atual.size()
        || acumulado.type() != atual.type() || canalAnterior != canal)
        acumulado = atual.clone();
    else
        cv::addWeighted(acumulado, rastro / 100.0, atual, 1.0 - rastro / 100.0, 0, acumulado);
    canalAnterior = canal;
    acumulado.convertTo(imagem, CV_8U);
    if (mirrorHorizontal || mirrorVertical)
        cv::flip(imagem, imagem, mirrorHorizontal && mirrorVertical ? -1 : (mirrorHorizontal ? 1 : 0));

    if (canal == Canal::Profundidade)
    {
        grayImage.setFromPixels(resultado);
        grayImage.brightnessContrast(brilho, contraste);
        resultado = grayImage.getPixels();
    }
    textura.loadData(resultado);
    return true;
}

void ProcessamentoKinect::draw(float x, float y, float width, float height)
{
    if (textura.isAllocated()) textura.draw(x, y, width, height);
}

void ProcessamentoKinect::drawMiniatura()
{
    draw(ofGetWidth() - 318, ofGetHeight() - 241, 308, 231);
}

void ProcessamentoKinect::drawControles()
{
    if (!ImGui::CollapsingHeader("Imagem Kinect")) return;
    int escolha = static_cast<int>(canal);
    ImGui::RadioButton("RGB", &escolha, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Profundidade", &escolha, 2);
    canal = static_cast<Canal>(escolha);
    ImGui::Checkbox("Espelho Horizontal", &mirrorHorizontal);
    ImGui::Checkbox("Espelho Vertical", &mirrorVertical);
    ImGui::SliderInt("blur", &blur, 0, 50);
    ImGui::SliderInt("blur forte", &blurForte, 0, 50);
    ImGui::SliderInt("erode", &erode, 0, 50);
    ImGui::SliderInt("dilate", &dilate, 0, 50);
    ImGui::SliderInt("Rastro", &rastro, 0, 99);
    if (canal == Canal::Profundidade)
    {
        ImGui::SliderFloat("brilho", &brilho, 0, 1);
        ImGui::SliderFloat("contraste", &contraste, 0, 1);
    }
}
