#ifndef ORDENADOR_HPP
#define ORDENADOR_HPP

#include "Database.hpp"
#include <vector>
#include <string>

class Ordenador {
private:
    long long comparacoes;
    long long trocas;

    int particionar(std::vector<FocoIncendio>& focos, int inicio, int fim, const std::string& criterio);
    void quickSort(std::vector<FocoIncendio>& focos, int inicio, int fim, const std::string& criterio);
    bool deveTrocar(const FocoIncendio& atual, const FocoIncendio& pivo, const std::string& criterio);

public:
    Ordenador();
    void ordenar(std::vector<FocoIncendio>& focos, const std::string& criterio);
    long long getComparacoes() const { return comparacoes; }
    long long getTrocas() const { return trocas; }
};

#endif
