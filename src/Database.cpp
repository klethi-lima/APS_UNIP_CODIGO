
#include "Database.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <mysqlx/xdevapi.h>

Database::Database() {}
Database::~Database() {}

bool Database::conectar(const std::string& host, const std::string& user, const std::string& pass, const std::string& db) {
    try {
        
        std::string conexaoUrl = "mysqlx://" + user + ":" + pass + "@" + host + ":3306/" + db;
        mysqlx::Session sess(conexaoUrl);
        return true;
    } catch (const mysqlx::Error &e) {
        std::cerr << "[ERRO SQL CONEXAO] " << e.what() << std::endl;
        return false;
    } catch (const std::exception &e) {
        std::cerr << "[ERRO INTERNO] Falha na alocacao de memoria: " << e.what() << std::endl;
        return false;
    }
}

std::vector<FocoIncendio> Database::buscarFocosParana() {
    std::vector<FocoIncendio> lista;
    try {
        mysqlx::Session sess("mysqlx://root:admin@127.0.0.1:3306/aps_queimadas");
        mysqlx::Schema db = sess.getSchema("aps_queimadas");
        mysqlx::Table tabela = db.getTable("focos_incendio");

        mysqlx::RowResult res = tabela.select("id", "data_foco", "bioma", "municipio", "latitude", "longitude").execute();

        for (mysqlx::Row row : res) {
            try {
                FocoIncendio foco;
                
                if (!row.get(0).isNull()) foco.id = static_cast<int>(row.get(0).get<int64_t>());
                else foco.id = 0;

                if (!row.get(1).isNull()) foco.data = row.get(1).get<std::string>();
                else foco.data = "";

                if (!row.get(2).isNull()) foco.bioma = row.get(2).get<std::string>();
                else foco.bioma = "";

                if (!row.get(3).isNull()) foco.municipio = row.get(3).get<std::string>();
                else foco.municipio = "";

                if (!row.get(4).isNull()) foco.latitude = row.get(4).get<double>();
                else foco.latitude = 0.0;

                if (!row.get(5).isNull()) foco.longitude = row.get(5).get<double>();
                else foco.longitude = 0.0;
                
                lista.push_back(foco);
            } catch (...) {
                continue;
            }
        }
    } catch (...) {
        std::cerr << "[ERRO SQL BUSCA] Erro ao processar tabela." << std::endl;
    }
    return lista;
}

bool Database::importarCSV(const std::string& caminhoArquivo) {
    std::ifstream arquivo(caminhoArquivo);
    std::string linha;

    if (!arquivo.is_open()) {
        std::cerr << "[ERRO] Nao foi possivel abrir o arquivo CSV especificado!" << std::endl;
        return false;
    }

    try {
        mysqlx::Session sess("mysqlx://root:admin@127.0.0.1:3306/aps_queimadas");
        mysqlx::Schema db = sess.getSchema("aps_queimadas");
        mysqlx::Table tabela = db.getTable("focos_incendio");

        std::getline(arquivo, linha); 

        auto insercao = tabela.insert("data_foco", "bioma", "municipio", "latitude", "longitude");
        int contador = 0;

        while (std::getline(arquivo, linha)) {
            if (linha.empty()) continue;

            std::stringstream ss(linha);
            std::string celula;
            std::vector<std::string> colunas;

            while (std::getline(ss, celula, ',')) {
                colunas.push_back(celula);
            }

            if (colunas.size() >= 8) {
                try {
                    std::string dataOriginal = colunas[0];
                    std::string dataClean = (dataOriginal.length() >= 10) ? dataOriginal.substr(0, 10) : dataOriginal;
                    
                    std::string municipio = colunas[4];
                    std::string bioma = colunas[5];
                    
                    double lat = 0.0;
                    double lon = 0.0;
                    if (!colunas[6].empty()) lat = std::stod(colunas[6]);
                    if (!colunas[7].empty()) lon = std::stod(colunas[7]);

                    insercao.values(dataClean, bioma, municipio, lat, lon);
                    contador++;

                    if (contador % 1000 == 0) {
                        insercao.execute();
                        insercao = tabela.insert("data_foco", "bioma", "municipio", "latitude", "longitude");
                    }
                } catch (...) {
                    continue;
                }
            }
        }

        if (contador % 1000 != 0) {
            insercao.execute();
        }

        std::cout << "[SUCESSO] Importacao concluida! " << contador << " registros inseridos no MySQL." << std::endl;
        arquivo.close();
        return true;

    } catch (...) {
        std::cerr << "[ERRO SQL IMPORTACAO] Falha ao injetar dados no MySQL." << std::endl;
        arquivo.close();
        return false;
    }
}
