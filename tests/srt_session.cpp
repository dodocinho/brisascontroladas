#include "TransmissorSrt.h"
#include "ofUtils.h"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char **argv) {
    assert(argc == 4);
    const bool expected = std::string(argv[2]) == "connected";
    TransmissorSrt stream(argv[3]);
    ofResetElapsedTimeCounter();
    stream.solicitarInicio("127.0.0.1",std::stoi(argv[1]),20);
    assert(stream.fase() != FaseSrt::Transmitindo);
    ofPixels pixels;
    pixels.allocate(800,600,OF_PIXELS_BGRA);
    pixels.set(80);
    bool confirmed = false;
    const auto end = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now()<end) {
        stream.atualizar(800,600);
        if (stream.aceitaQuadro()) stream.enviar(pixels);
        confirmed |= stream.fase() == FaseSrt::Transmitindo;
        if (!expected) assert(!confirmed);
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    assert(confirmed == expected);
    stream.solicitarParada();
    assert(stream.fase() == FaseSrt::Desligada);
    std::cout << "PASS real SRT " << (expected ? "output confirmed by FFmpeg" : "unavailable listener never reported active") << '\n';
}
