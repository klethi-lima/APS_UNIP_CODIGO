#define SFML_DYNAMIC
#define IMGUI_API __declspec(dllimport)
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <SFML/Graphics.hpp>
#include "imgui.h"
#include "imgui-sfml.h"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <ctime>
#include <future>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

struct FocoIncendioLocal {
    int id;
    string data;
    string satelite;
    string municipio;
    string bioma;
    double latitude;
    double longitude;
};

struct EstatisticasFocos {
    int total = 0;
    string municipioMaisAtivo = "N/A";
    int ocorrenciasMunicipioMaisAtivo = 0;
};

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~WinHttpHandle() {
        if (handle_) WinHttpCloseHandle(handle_);
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    HINTERNET get() const { return handle_; }

private:
    HINTERNET handle_;
};

static void lancarErroWinHttp(const char* operacao) {
    throw runtime_error(string(operacao) + " (erro do Windows " + to_string(GetLastError()) + ")");
}

static string baixarCsvInpe() {
    const time_t agora = chrono::system_clock::to_time_t(chrono::system_clock::now());
    tm dataUtc{};
    gmtime_s(&dataUtc, &agora);
    wostringstream caminho;
    caminho << L"/queimadas/queimadas/focos/csv/diario/Brasil/focos_diario_br_"
            << setfill(L'0') << setw(4) << dataUtc.tm_year + 1900
            << setw(2) << dataUtc.tm_mon + 1
            << setw(2) << dataUtc.tm_mday << L".csv";

    WinHttpHandle sessao(WinHttpOpen(
        L"APS-UNIP-Queimadas/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));
    if (!sessao.get()) lancarErroWinHttp("Nao foi possivel iniciar a conexao HTTP");

    if (!WinHttpSetTimeouts(sessao.get(), 10000, 10000, 10000, 20000)) {
        lancarErroWinHttp("Nao foi possivel configurar o tempo limite da conexao");
    }

    WinHttpHandle conexao(WinHttpConnect(
        sessao.get(),
        L"dataserver-coids.inpe.br",
        INTERNET_DEFAULT_HTTPS_PORT,
        0));
    if (!conexao.get()) lancarErroWinHttp("Nao foi possivel conectar ao servidor do INPE");

    WinHttpHandle requisicao(WinHttpOpenRequest(
        conexao.get(),
        L"GET",
        caminho.str().c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE));
    if (!requisicao.get()) lancarErroWinHttp("Nao foi possivel criar a requisicao ao INPE");

    if (!WinHttpSendRequest(
            requisicao.get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(requisicao.get(), nullptr)) {
        lancarErroWinHttp("Falha ao receber dados do INPE");
    }

    DWORD codigoStatus = 0;
    DWORD tamanhoStatus = sizeof(codigoStatus);
    if (!WinHttpQueryHeaders(
            requisicao.get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &codigoStatus,
            &tamanhoStatus,
            WINHTTP_NO_HEADER_INDEX)) {
        lancarErroWinHttp("Nao foi possivel verificar a resposta do INPE");
    }
    if (codigoStatus < 200 || codigoStatus >= 300) {
        throw runtime_error("O servidor do INPE respondeu com HTTP " + to_string(codigoStatus));
    }

    string conteudo;
    constexpr size_t limiteResposta = 50 * 1024 * 1024;
    char bloco[8192];
    for (;;) {
        DWORD disponivel = 0;
        if (!WinHttpQueryDataAvailable(requisicao.get(), &disponivel)) {
            lancarErroWinHttp("Falha ao consultar o tamanho da resposta do INPE");
        }
        if (disponivel == 0) break;

        while (disponivel > 0) {
            DWORD lidos = 0;
            const DWORD tamanhoBloco = min<DWORD>(disponivel, sizeof(bloco));
            if (!WinHttpReadData(requisicao.get(), bloco, tamanhoBloco, &lidos)) {
                lancarErroWinHttp("Falha ao ler os dados do INPE");
            }
            if (lidos == 0) break;
            if (conteudo.size() + lidos > limiteResposta) {
                throw runtime_error("A resposta do INPE excede o limite de 50 MB");
            }
            conteudo.append(bloco, lidos);
            disponivel -= lidos;
        }
    }

    if (conteudo.empty()) throw runtime_error("O INPE retornou um arquivo vazio");
    return conteudo;
}

static string aparar(const string& valor) {
    const size_t inicio = valor.find_first_not_of(" \t\r\n");
    if (inicio == string::npos) return "";
    const size_t fim = valor.find_last_not_of(" \t\r\n");
    return valor.substr(inicio, fim - inicio + 1);
}

static vector<string> separarCamposCsv(const string& linha) {
    vector<string> campos;
    string campo;
    bool entreAspas = false;

    for (size_t i = 0; i < linha.size(); ++i) {
        const char caractere = linha[i];
        if (caractere == '"') {
            if (entreAspas && i + 1 < linha.size() && linha[i + 1] == '"') {
                campo += '"';
                ++i;
            } else {
                entreAspas = !entreAspas;
            }
        } else if (caractere == ',' && !entreAspas) {
            campos.push_back(move(campo));
            campo.clear();
        } else {
            campo += caractere;
        }
    }

    if (entreAspas) throw runtime_error("O CSV do INPE contem um campo entre aspas incompleto");
    campos.push_back(move(campo));
    return campos;
}

static vector<FocoIncendioLocal> interpretarFocosParana(const string& csv) {
    vector<FocoIncendioLocal> focos;
    istringstream linhas(csv);
    string linha;
    if (!getline(linhas, linha)) throw runtime_error("O CSV do INPE nao contem cabecalho");

    vector<string> cabecalho = separarCamposCsv(linha);
    if (!cabecalho.empty() && cabecalho[0].size() >= 3 &&
        static_cast<unsigned char>(cabecalho[0][0]) == 0xEF &&
        static_cast<unsigned char>(cabecalho[0][1]) == 0xBB &&
        static_cast<unsigned char>(cabecalho[0][2]) == 0xBF) {
        cabecalho[0].erase(0, 3);
    }

    map<string, size_t> indices;
    for (size_t i = 0; i < cabecalho.size(); ++i) indices[aparar(cabecalho[i])] = i;
    const auto indiceColuna = [&indices](const string& nome) {
        const auto encontrado = indices.find(nome);
        if (encontrado == indices.end()) {
            throw runtime_error("O CSV do INPE nao possui a coluna " + nome);
        }
        return encontrado->second;
    };

    const size_t indiceLatitude = indiceColuna("lat");
    const size_t indiceLongitude = indiceColuna("lon");
    const size_t indiceData = indiceColuna("data_hora_gmt");
    const size_t indiceSatelite = indiceColuna("satelite");
    const size_t indiceMunicipio = indiceColuna("municipio");
    const size_t indiceEstado = indiceColuna("estado");
    const size_t indiceBioma = indiceColuna("bioma");
    const size_t maiorIndice = max({indiceLatitude, indiceLongitude, indiceData, indiceSatelite,
                                    indiceMunicipio, indiceEstado, indiceBioma});

    while (getline(linhas, linha)) {
        if (!linha.empty() && linha.back() == '\r') linha.pop_back();
        if (linha.empty()) continue;

        const vector<string> campos = separarCamposCsv(linha);
        if (campos.size() <= maiorIndice) {
            throw runtime_error("O CSV do INPE contem uma linha incompleta");
        }

        string estado = aparar(campos[indiceEstado]);
        transform(estado.begin(), estado.end(), estado.begin(),
                  [](unsigned char caractere) { return static_cast<char>(toupper(caractere)); });
        if (estado.find("PARAN") != 0) continue;

        FocoIncendioLocal foco{};
        foco.id = static_cast<int>(focos.size()) + 1;
        foco.data = aparar(campos[indiceData]);
        foco.satelite = aparar(campos[indiceSatelite]);
        foco.municipio = aparar(campos[indiceMunicipio]);
        foco.bioma = aparar(campos[indiceBioma]);
        if (foco.municipio.empty() || foco.data.empty() || foco.bioma.empty()) {
            throw runtime_error("O INPE retornou um foco do Parana sem municipio, data ou bioma");
        }
        try {
            foco.latitude = stod(aparar(campos[indiceLatitude]));
            foco.longitude = stod(aparar(campos[indiceLongitude]));
        } catch (const exception&) {
            throw runtime_error("O INPE retornou coordenadas invalidas para um foco do Parana");
        }
        focos.push_back(move(foco));
    }

    return focos;
}

static string paraMinusculas(string texto) {
    transform(texto.begin(), texto.end(), texto.begin(),
              [](unsigned char caractere) { return static_cast<char>(tolower(caractere)); });
    return texto;
}

static EstatisticasFocos calcularEstatisticas(const vector<FocoIncendioLocal>& focos) {
    EstatisticasFocos stats;
    map<string, int> contagemMunicipio;
    stats.total = static_cast<int>(focos.size());

    for (const auto& foco : focos) contagemMunicipio[foco.municipio]++;
    for (const auto& [municipio, total] : contagemMunicipio) {
        if (total > stats.ocorrenciasMunicipioMaisAtivo) {
            stats.municipioMaisAtivo = municipio;
            stats.ocorrenciasMunicipioMaisAtivo = total;
        }
    }
    return stats;
}

static string horarioLocal(chrono::system_clock::time_point instante) {
    const time_t tempo = chrono::system_clock::to_time_t(instante);
    tm partes{};
    localtime_s(&partes, &tempo);
    ostringstream texto;
    texto << put_time(&partes, "%d/%m/%Y %H:%M:%S");
    return texto.str();
}

static string formatarDataHoraUTC(const string& data) {
    if (data.size() < 16 || data[4] != '-' || data[7] != '-' || data[10] != ' ') {
        return data;
    }

    return data.substr(8, 2) + "/" + data.substr(5, 2) + "/" + data.substr(0, 4) +
           " " + data.substr(11, 5) + " UTC";
}

static string formatarCoordenada(double valor) {
    ostringstream texto;
    texto.imbue(locale::classic());
    texto << fixed << setprecision(3) << valor;
    return texto.str();
}

int main() {
    vector<FocoIncendioLocal> listaFocos;
    bool ordenado = false;
    bool solicitarAtualizacao = true;
    string municipioFiltro;
    char municipioBuffer[64] = {};
    string statusAtualizacao = "Conectando ao feed oficial do INPE...";
    string ultimaAtualizacao = "Ainda sem dados carregados";
    const auto intervaloAtualizacao = chrono::minutes(10);
    auto proximaAtualizacao = chrono::steady_clock::now();
    auto horarioUltimaAtualizacao = chrono::system_clock::time_point{};
    future<vector<FocoIncendioLocal>> tarefaAtualizacao;

    sf::RenderWindow window(sf::VideoMode({1280, 720}), "APS UNIP - Focos de Queimadas do Parana");
    window.setFramerateLimit(60);

    if (!ImGui::SFML::Init(window)) return -1;

    ImFontConfig fonteConfig;
    fonteConfig.SizePixels = 14.0f;
    ImGui::GetIO().Fonts->AddFontDefaultVector(&fonteConfig);
    ImGui::StyleColorsDark();
    ImGuiStyle& estilo = ImGui::GetStyle();
    estilo.FontScaleMain = 1.0f;
    estilo.FramePadding = ImVec2(7.0f, 4.0f);
    estilo.CellPadding = ImVec2(7.0f, 4.0f);
    estilo.ItemSpacing = ImVec2(7.0f, 6.0f);

    sf::Clock deltaClock;
    while (window.isOpen()) {
        while (const auto event = window.pollEvent()) {
            ImGui::SFML::ProcessEvent(window, *event);
            if (event->is<sf::Event::Closed>()) window.close();
        }

        ImGui::SFML::Update(window, deltaClock.restart());

        if (tarefaAtualizacao.valid() &&
            tarefaAtualizacao.wait_for(chrono::seconds(0)) == future_status::ready) {
            try {
                listaFocos = tarefaAtualizacao.get();
                ordenado = false;
                horarioUltimaAtualizacao = chrono::system_clock::now();
                ultimaAtualizacao = horarioLocal(horarioUltimaAtualizacao);
                statusAtualizacao = "Dados oficiais do INPE carregados";
            } catch (const exception& erro) {
                statusAtualizacao = string("Falha ao atualizar: ") + erro.what();
            }
            proximaAtualizacao = chrono::steady_clock::now() + intervaloAtualizacao;
        }

        if (!tarefaAtualizacao.valid() &&
            (solicitarAtualizacao || chrono::steady_clock::now() >= proximaAtualizacao)) {
            solicitarAtualizacao = false;
            statusAtualizacao = "Consultando o feed oficial do INPE...";
            tarefaAtualizacao = async(launch::async, [] {
                return interpretarFocosParana(baixarCsvInpe());
            });
        }

        vector<FocoIncendioLocal> focosExibidos;
        focosExibidos.reserve(listaFocos.size());
        const string filtroMinusculo = paraMinusculas(municipioFiltro);
        for (const auto& foco : listaFocos) {
            if (filtroMinusculo.empty() ||
                paraMinusculas(foco.municipio).find(filtroMinusculo) != string::npos) {
                focosExibidos.push_back(foco);
            }
        }
        const EstatisticasFocos estatisticas = calcularEstatisticas(focosExibidos);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Painel Analitico", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.0f, 1.0f),
                           "MONITORAMENTO DE FOCOS DE QUEIMADAS - PARANA");
        ImGui::Text("Fonte: INPE / Programa Queimadas (arquivo diario oficial)");
        ImGui::Separator();

        ImGui::BeginChild("Controles", ImVec2(320, 0), true);
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.75f, 1.0f, 1.0f), "Atualizacao dos dados");
        ImGui::TextWrapped("%s", statusAtualizacao.c_str());
        ImGui::Text("Ultima carga bem-sucedida:");
        ImGui::TextUnformatted(ultimaAtualizacao.c_str());
        ImGui::Text("Consulta automatica a cada dez minutos");
        if (ImGui::Button("Atualizar agora", ImVec2(280, 36))) solicitarAtualizacao = true;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 0.75f, 1.0f, 1.0f), "Filtros");
        ImGui::InputText("Municipio", municipioBuffer, sizeof(municipioBuffer));
        municipioFiltro = municipioBuffer;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Metricas dos dados exibidos");
        ImGui::Text("Focos no filtro:");
        ImGui::SameLine();
        ImGui::TextUnformatted(to_string(estatisticas.total).c_str());
        ImGui::Text("Focos no Parana:");
        ImGui::SameLine();
        ImGui::TextUnformatted(to_string(listaFocos.size()).c_str());
        ImGui::Text("Municipio com mais focos: %s", estatisticas.municipioMaisAtivo.c_str());
        ImGui::Text("Focos nesse municipio:");
        ImGui::SameLine();
        ImGui::TextUnformatted(to_string(estatisticas.ocorrenciasMunicipioMaisAtivo).c_str());

        if (ordenado) {
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Tabela ordenada por municipio");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("TabelaDados", ImVec2(0, 0), true);
        ImGui::Text("Focos de calor detectados no Parana");
        ImGui::SameLine();
        const string totalRegistros = "[" + to_string(estatisticas.total) + " registros]";
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.7f, 1.0f), "%s", totalRegistros.c_str());
        if (ImGui::Button("Ordenar por municipio")) {
            sort(listaFocos.begin(), listaFocos.end(), [](const FocoIncendioLocal& a, const FocoIncendioLocal& b) {
                if (a.municipio == b.municipio) return a.id < b.id;
                return a.municipio < b.municipio;
            });
            ordenado = true;
        }
        ImGui::Spacing();

        const float alturaTabela = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginTable("TabelaFocos", 7,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                              ImVec2(0, alturaTabela))) {
            ImGui::TableSetupColumn("Seq.", ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Data/hora UTC", ImGuiTableColumnFlags_WidthFixed, 205.0f);
            ImGui::TableSetupColumn("Satelite", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Municipio", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Bioma", ImGuiTableColumnFlags_WidthFixed, 135.0f);
            ImGui::TableSetupColumn("Lat. (graus)", ImGuiTableColumnFlags_WidthFixed, 105.0f);
            ImGui::TableSetupColumn("Long. (graus)", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableHeadersRow();

            for (const auto& foco : focosExibidos) {
                ImGui::TableNextRow();
                const string dataFormatada = formatarDataHoraUTC(foco.data);
                const string idFormatado = to_string(foco.id);
                const string latitudeFormatada = formatarCoordenada(foco.latitude);
                const string longitudeFormatada = formatarCoordenada(foco.longitude);
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(idFormatado.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(dataFormatada.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(foco.satelite.c_str());
                ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(foco.municipio.c_str());
                ImGui::TableSetColumnIndex(4); ImGui::TextUnformatted(foco.bioma.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(latitudeFormatada.c_str());
                ImGui::TableSetColumnIndex(6);
                ImGui::TextUnformatted(longitudeFormatada.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::End();

        window.clear();
        ImGui::SFML::Render(window);
        window.display();
    }

    ImGui::SFML::Shutdown(window);
    return 0;
}
