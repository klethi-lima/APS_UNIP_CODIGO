#ifndef DATABASE_HPP
#define DATABASE_HPP

#include <vector>
#include <string>

struct FocoIncendio {
    int id;
    std::string data;
    std::string bioma;
    std::string municipio;
    double latitude;
    double longitude;
};

class Database {
public:
    Database();
    ~Database();
    bool conectar(const std::string& host, const std::string& user, const std::string& pass, const std::string& db);
    std::vector<FocoIncendio> buscarFocosParana();
    
   
    bool importarCSV(const std::string& caminhoArquivo);
};

#endif
