#include "Ordenador.hpp"
#include <algorithm>

Ordenador::Ordenador() : comparacoes(0), trocas(0) {}

void Ordenador::ordenar(std::vector<FocoIncendio>& focos, const std::string& criterio) {
    comparacoes = 0;
    trocas = 0;
    if (focos.empty()) return;
    quickSort(focos, 0, focos.size() - 1, criterio);
}

void Ordenador::quickSort(std::vector<FocoIncendio>& focos, int inicio, int fim, const std::string& criterio) {
    if (inicio < fim) {
        int posicaoPivo = particionar(focos, inicio, fim, criterio);
        quickSort(focos, inicio, posicaoPivo - 1, criterio);
        quickSort(focos, posicaoPivo + 1, fim, criterio);
    }
}

int Ordenador::particionar(std::vector<FocoIncendio>& focos, int inicio, int fim, const std::string& criterio) {
    FocoIncendio pivo = focos[fim];
    int i = inicio - 1;

    for (int j = inicio; j < fim; j++) {
        comparacoes++;
        if (deveTrocar(focos[j], pivo, criterio)) {
            i++;
            std::swap(focos[i], focos[j]);
            trocas++;
        }
    }
    std::swap(focos[i + 1], focos[fim]);
    trocas++;
    return i + 1;
}

bool Ordenador::deveTrocar(const FocoIncendio& atual, const FocoIncendio& pivo, const std::string& criterio) {
    if (criterio == "municipio") {
        return atual.municipio < pivo.municipio;
    } else if (criterio == "bioma") {
        return atual.bioma < pivo.bioma;
    } else {
        return atual.data < pivo.data;
    }
}
