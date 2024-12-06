#include <boost/asio.hpp>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <ctime>
#include <utility>
#include <filesystem>

using boost::asio::ip::tcp;

std::vector<std::string> sensores_registrados;
constexpr std::string_view MENSAGEM_SENSOR_INVALIDO = "ERRO|ID_SENSOR_INVALIDO\r\n";

// Prototipação das funções
void salvar_dados_log(const std::string& mensagem_log);
std::string criar_resposta(const std::string& requisicao);
bool verificar_sensor_registrado(const std::vector<std::string>& lista, std::string_view id_sensor);
void registrar_erro(std::string_view mensagem_erro);

#pragma pack(push, 1)
struct RegistroLog {
    char id_sensor[32];
    std::time_t momento;
    double leitura;
};
#pragma pack(pop)

std::time_t converter_para_tempo(const std::string& str_tempo);
std::string converter_para_string(std::time_t tempo);
void analisar_log(const std::string& mensagem, RegistroLog& registro);
void analisar_requisicao(const std::string& requisicao, char (&id_sensor)[32], int& total_registros);

class Sessao : public std::enable_shared_from_this<Sessao> {
public:
    explicit Sessao(tcp::socket cliente_socket) 
        : cliente_socket_(std::move(cliente_socket)) {}

    void iniciar() { receber_mensagens(); }

private:
    tcp::socket cliente_socket_;
    boost::asio::streambuf buffer_entrada_;

    void receber_mensagens() {
        auto self = shared_from_this();
        boost::asio::async_read_until(cliente_socket_, buffer_entrada_, "\r\n",
            [this, self](boost::system::error_code ec, std::size_t bytes_lidos) {
                if (!ec) {
                    std::istream fluxo_entrada(&buffer_entrada_);
                    std::string mensagem_recebida((std::istreambuf_iterator<char>(fluxo_entrada)), {});

                    std::cout << "Mensagem recebida: " << mensagem_recebida << '\n';
                    processar_mensagem(mensagem_recebida);
                }
            });
    }

void processar_mensagem(const std::string& mensagem) {
    auto comando = mensagem.substr(0, 3);

    if (comando == "LOG") {
        RegistroLog registro;
        analisar_log(mensagem, registro);
        if (!verificar_sensor_registrado(sensores_registrados, registro.id_sensor)) {
            sensores_registrados.emplace_back(registro.id_sensor);
        }
        salvar_dados_log(mensagem);
    } else if (comando == "GET") {
        char id_sensor[32];
        int total;
        analisar_requisicao(mensagem, id_sensor, total);
        if (!verificar_sensor_registrado(sensores_registrados, id_sensor)) {
            enviar_mensagem(std::string(MENSAGEM_SENSOR_INVALIDO)); // Conversão aqui
        } else {
            enviar_mensagem(criar_resposta(mensagem));
        }
    }

    receber_mensagens();
}

    void enviar_mensagem(const std::string& mensagem) {
        auto self = shared_from_this();
        boost::asio::async_write(cliente_socket_, boost::asio::buffer(mensagem),
            [this, self](boost::system::error_code ec, std::size_t) {
                if (!ec) { receber_mensagens(); }
            });
    }
};

class Servidor {
public:
    Servidor(boost::asio::io_context& contexto, unsigned short porta)
        : receptor_(contexto, tcp::endpoint(tcp::v4(), porta)) {
        aceitar_conexoes();
    }

private:
    tcp::acceptor receptor_;

    void aceitar_conexoes() {
        receptor_.async_accept([this](boost::system::error_code ec, tcp::socket socket) {
            if (!ec) {
                std::make_shared<Sessao>(std::move(socket))->iniciar();
            }
            aceitar_conexoes();
        });
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Uso: servidor <porta>\n";
        return EXIT_FAILURE;
    }

    boost::asio::io_context contexto_io;
    Servidor servidor(contexto_io, std::atoi(argv[1]));
    contexto_io.run();

    return EXIT_SUCCESS;
}

void salvar_dados_log(const std::string& mensagem_log) {
    RegistroLog registro;
    analisar_log(mensagem_log, registro);
    auto nome_arquivo = std::string(registro.id_sensor) + ".dat";
    std::ofstream arquivo(nome_arquivo, std::ios::binary | std::ios::app);

    if (arquivo) {
        arquivo.write(reinterpret_cast<const char*>(&registro), sizeof(registro));
    } else {
        registrar_erro("Erro ao salvar dados do log.");
    }
}

std::string criar_resposta(const std::string& requisicao) {
    char id_sensor[32];
    int num_registros;
    analisar_requisicao(requisicao, id_sensor, num_registros);

    std::string resposta;
    std::ifstream arquivo(std::string(id_sensor) + ".dat", std::ios::binary);

    if (arquivo) {
        RegistroLog registro;
        while (arquivo.read(reinterpret_cast<char*>(&registro), sizeof(registro)) && num_registros-- > 0) {
            resposta += "Sensor: " + std::string(registro.id_sensor) +
                        ", Tempo: " + converter_para_string(registro.momento) +
                        ", Valor: " + std::to_string(registro.leitura) + '\n';
        }
    } else {
        registrar_erro("Erro ao abrir arquivo para leitura.");
    }

    return resposta;
}

bool verificar_sensor_registrado(const std::vector<std::string>& lista, std::string_view id_sensor) {
    return std::find(lista.begin(), lista.end(), id_sensor) != lista.end();
}

void registrar_erro(std::string_view mensagem_erro) {
    std::cerr << mensagem_erro << '\n';
}

std::time_t converter_para_tempo(const std::string& str_tempo) {
    std::tm estrutura_tempo = {};
    std::istringstream fluxo(str_tempo);
    fluxo >> std::get_time(&estrutura_tempo, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&estrutura_tempo);
}

std::string converter_para_string(std::time_t tempo) {
    std::tm* estrutura_tempo = std::localtime(&tempo);
    std::ostringstream fluxo;
    fluxo << std::put_time(estrutura_tempo, "%Y-%m-%dT%H:%M:%S");
    return fluxo.str();
}


void analisar_log(const std::string& mensagem, RegistroLog& registro) {
    std::istringstream fluxo(mensagem);
    std::string campo;

    std::getline(fluxo, campo, '|'); // Ignora "LOG"
    std::getline(fluxo, campo, '|'); // Lê o id_sensor
    std::strncpy(registro.id_sensor, campo.c_str(), sizeof(registro.id_sensor));

    std::getline(fluxo, campo, '|'); // Lê o momento
    registro.momento = converter_para_tempo(campo);

    std::getline(fluxo, campo, '|'); // Lê a leitura
    registro.leitura = std::stod(campo);
}

void analisar_requisicao(const std::string& requisicao, char (&id_sensor)[32], int& total_registros) {
    std::istringstream fluxo(requisicao);
    std::string campo;

    std::getline(fluxo, campo, '|'); // Ignora "GET"
    std::getline(fluxo, campo, '|'); // Lê o id_sensor
    std::strncpy(id_sensor, campo.c_str(), sizeof(id_sensor));

    std::getline(fluxo, campo, '|'); // Lê o total de registros
    total_registros = std::stoi(campo);
}

