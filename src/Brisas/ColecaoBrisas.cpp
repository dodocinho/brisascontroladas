#include "ColecaoBrisas.h"
#include <algorithm>

BrisaId ColecaoBrisas::adiciona(std::unique_ptr<Brisa> brisa)
{
    if (!brisa) return 0;
    const BrisaId id = proximoId++;
    brisa->id = id;
    brisa->colecao = this;
    brisas.push_back(std::move(brisa));
    return id;
}

bool ColecaoBrisas::remove(BrisaId id)
{
    auto it = std::find_if(brisas.begin(), brisas.end(),
        [id](const auto &brisa) { return brisa->getId() == id; });
    if (it == brisas.end()) return false;
    for (const auto &brisa : brisas)
        if (brisa->brisaShader == id) brisa->brisaShader = 0;
    brisas.erase(it);
    return true;
}

bool ColecaoBrisas::move(BrisaId id, int direcao)
{
    auto it = std::find_if(brisas.begin(), brisas.end(),
        [id](const auto &brisa) { return brisa->getId() == id; });
    if (it == brisas.end() || direcao == 0) return false;
    const auto posicao = std::distance(brisas.begin(), it);
    const auto tamanho = static_cast<std::ptrdiff_t>(brisas.size());
    const auto destino = (posicao + (direcao > 0 ? 1 : -1) + tamanho) % tamanho;
    std::iter_swap(it, brisas.begin() + destino);
    return true;
}

Brisa *ColecaoBrisas::busca(BrisaId id) const
{
    for (const auto &brisa : brisas)
        if (brisa->getId() == id) return brisa.get();
    return nullptr;
}

void ColecaoBrisas::clear()
{
    brisas.clear();
    // Não reutilizar IDs: referências antigas não podem apontar para novas camadas.
}
