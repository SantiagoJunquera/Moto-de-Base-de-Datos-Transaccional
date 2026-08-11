#include <iostream>
#include "client_console.hpp"

int main(int argc, char* argv[]) {
    txdb::network::InitializeNetworking();

    std::string host = "127.0.0.1";
    uint16_t port = 9001;

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = static_cast<uint16_t>(std::atoi(argv[2]));
    }

    std::cout << "[TxClient] Conectando a TxKernel en " << host << ":" << port << "..." << std::endl;

    txdb::client::ClientConsole console;
    if (!console.connect(host, port)) {
        std::cerr << "[TxClient] Error: No se pudo establecer conexión con TxKernel en " 
                  << host << ":" << port << ". Asegúrese de que el servidor esté corriendo." << std::endl;
        txdb::network::CleanupNetworking();
        return 1;
    }

    std::cout << "[TxClient] Conexión establecida exitosamente." << std::endl;
    console.run_repl();

    txdb::network::CleanupNetworking();
    return 0;
}
