#pragma once

#include "Brisa.h"
#include <memory>
#include <vector>

// Única dona das camadas. Referências externas usam IDs, nunca posições no vetor.
class ColecaoBrisas
{
public:
    ColecaoBrisas() = default;
    ColecaoBrisas(const ColecaoBrisas &) = delete;
    ColecaoBrisas &operator=(const ColecaoBrisas &) = delete;

    BrisaId adiciona(std::unique_ptr<Brisa> brisa);
    bool remove(BrisaId id);
    bool move(BrisaId id, int direcao);
    Brisa *busca(BrisaId id) const;
    void clear();
    const std::vector<std::unique_ptr<Brisa>> &getBrisas() const { return brisas; }

private:
    std::vector<std::unique_ptr<Brisa>> brisas;
    BrisaId proximoId = 1;
};
