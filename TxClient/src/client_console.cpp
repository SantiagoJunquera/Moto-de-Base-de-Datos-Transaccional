#include "client_console.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <vector>

namespace txdb::client {

ClientConsole::ClientConsole() = default;

ClientConsole::~ClientConsole() {
    disconnect();
}

bool ClientConsole::connect(const std::string& host, uint16_t port) {
    socket_ = std::make_unique<txdb::network::Socket>();
    if (!socket_->connect(host, port)) {
        socket_.reset();
        return false;
    }
    return true;
}

void ClientConsole::disconnect() {
    if (socket_ && socket_->is_valid()) {
        socket_->close();
    }
    socket_.reset();
    in_transaction_ = false;
    current_tx_id_ = 0;
}

bool ClientConsole::is_connected() const {
    return socket_ && socket_->is_valid();
}

std::string ClientConsole::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

void ClientConsole::print_banner() const {
    std::cout << "=======================================================\n";
    std::cout << "     TxDB-OS Distribuido - Cliente Interactivo (REPL)  \n";
    std::cout << "=======================================================\n";
    std::cout << " Comandos disponibles:\n";
    std::cout << "   BEGIN       - Iniciar Transacción\n";
    std::cout << "   COMMIT      - Confirmar Transacción\n";
    std::cout << "   ROLLBACK    - Abortar Transacción\n";
    std::cout << "   <Query DML> - SELECT, UPDATE, INSERT, DELETE\n";
    std::cout << "   EXIT        - Salir de la consola\n";
    std::cout << "-------------------------------------------------------\n";
}

void ClientConsole::run_repl() {
    print_banner();

    std::string line;
    while (is_connected()) {
        if (in_transaction_) {
            std::cout << "txdb [Tx #" << current_tx_id_ << "]> ";
        } else {
            std::cout << "txdb> ";
        }

        if (!std::getline(std::cin, line)) {
            break;
        }

        std::string cmd = trim(line);
        if (cmd.empty()) continue;

        if (cmd == "EXIT" || cmd == "exit" || cmd == "QUIT" || cmd == "quit") {
            std::cout << "[TxClient] Desconectando...\n";
            disconnect();
            break;
        }

        if (!execute_command(cmd)) {
            std::cout << "[TxClient] Error procesando el comando o conexión perdida.\n";
            if (!is_connected()) break;
        }
    }
}

bool ClientConsole::execute_command(const std::string& line) {
    if (!is_connected()) return false;

    std::string cmd = trim(line);
    std::string cmd_upper = cmd;
    std::transform(cmd_upper.begin(), cmd_upper.end(), cmd_upper.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });

    if (cmd_upper == "BEGIN" || cmd_upper == "BEGIN TRANSACTION" || cmd_upper == "BEGIN WORK") {
        protocol::MessageHeader hdr{protocol::Opcode::BEGIN_TX, 0};
        if (!socket_->send_all(&hdr, sizeof(hdr))) return false;

        protocol::MessageHeader res{};
        if (!socket_->recv_all(&res, sizeof(res)) || res.opcode != protocol::Opcode::TX_SUCCESS) return false;

        protocol::TransactionPayload payload{};
        if (!socket_->recv_all(&payload, sizeof(payload))) return false;

        current_tx_id_ = payload.tx_id;
        in_transaction_ = true;
        std::cout << "[TxClient] Transacción iniciada con éxito. Transaction ID: " << current_tx_id_ << "\n";
        return true;
    }

    if (cmd_upper == "COMMIT" || cmd_upper == "COMMIT TRANSACTION" || cmd_upper == "COMMIT WORK") {
        protocol::MessageHeader hdr{protocol::Opcode::COMMIT_TX, 0};
        if (!socket_->send_all(&hdr, sizeof(hdr))) return false;

        protocol::MessageHeader res{};
        if (!socket_->recv_all(&res, sizeof(res))) return false;

        if (res.opcode == protocol::Opcode::TX_SUCCESS) {
            protocol::TransactionPayload payload{};
            socket_->recv_all(&payload, sizeof(payload));
            std::cout << "[TxClient] Transacción #" << current_tx_id_ << " confirmada (COMMITTED) exitosamente.\n";
        } else {
            std::cout << "[TxClient] Error en COMMIT de transacción #" << current_tx_id_ << "\n";
        }

        in_transaction_ = false;
        current_tx_id_ = 0;
        return true;
    }

    if (cmd_upper == "ROLLBACK" || cmd_upper == "ROLLBACK TRANSACTION" || cmd_upper == "ROLLBACK WORK") {
        protocol::MessageHeader hdr{protocol::Opcode::ROLLBACK_TX, 0};
        if (!socket_->send_all(&hdr, sizeof(hdr))) return false;

        protocol::MessageHeader res{};
        if (!socket_->recv_all(&res, sizeof(res))) return false;

        protocol::TransactionPayload payload{};
        socket_->recv_all(&payload, sizeof(payload));
        std::cout << "[TxClient] Transacción #" << current_tx_id_ << " abortada (ROLLBACK).\n";

        in_transaction_ = false;
        current_tx_id_ = 0;
        return true;
    }

    // De lo contrario, tratar como sentencia DML (SELECT, UPDATE, INSERT, DELETE)
    protocol::MessageHeader hdr{protocol::Opcode::QUERY, static_cast<uint32_t>(cmd.size())};
    if (!socket_->send_all(&hdr, sizeof(hdr)) || !socket_->send_all(cmd.data(), cmd.size())) return false;

    protocol::MessageHeader res{};
    if (!socket_->recv_all(&res, sizeof(res))) return false;

    if (res.opcode == protocol::Opcode::QUERY_RESULT) {
        if (res.payload_size == protocol::PAGE_SIZE) {
            std::vector<uint8_t> page_buffer(protocol::PAGE_SIZE);
            if (socket_->recv_all(page_buffer.data(), protocol::PAGE_SIZE)) {
                if (cmd.find("productos") != std::string::npos || cmd.find("PRODUCTOS") != std::string::npos) {
                    const protocol::ProductRecord* prods = reinterpret_cast<const protocol::ProductRecord*>(page_buffer.data());
                    size_t max_records = protocol::PAGE_SIZE / sizeof(protocol::ProductRecord);

                    std::cout << "\n+----+--------------------------------+--------+\n";
                    std::cout << "| ID | Producto                       | Precio |\n";
                    std::cout << "+----+--------------------------------+--------+\n";

                    size_t count = 0;
                    for (size_t i = 0; i < max_records; ++i) {
                        if (prods[i].is_active && prods[i].id != 0) {
                            char line_buf[128];
                            std::snprintf(line_buf, sizeof(line_buf), "| %-2u | %-30s | $%5d |\n", 
                                          prods[i].id, prods[i].nombre, prods[i].precio);
                            std::cout << line_buf;
                            count++;
                        }
                    }
                    std::cout << "+----+--------------------------------+--------+\n";
                    std::cout << " (" << count << " fila(s) leída(s) de la tabla 'productos' en txdb_storage.dat)\n\n";
                } else {
                    const protocol::UserRecord* users = reinterpret_cast<const protocol::UserRecord*>(page_buffer.data());
                    size_t max_records = protocol::PAGE_SIZE / sizeof(protocol::UserRecord);

                    std::cout << "\n+----+--------------------------------+--------+\n";
                    std::cout << "| ID | Nombre                         | Saldo  |\n";
                    std::cout << "+----+--------------------------------+--------+\n";

                    size_t count = 0;
                    for (size_t i = 0; i < max_records; ++i) {
                        if (users[i].is_active && users[i].id != 0) {
                            char line_buf[128];
                            std::snprintf(line_buf, sizeof(line_buf), "| %-2u | %-30s | %6d |\n", 
                                          users[i].id, users[i].nombre, users[i].saldo);
                            std::cout << line_buf;
                            count++;
                        }
                    }
                    std::cout << "+----+--------------------------------+--------+\n";
                    std::cout << " (" << count << " fila(s) leída(s) de la tabla 'usuarios' en txdb_storage.dat)\n\n";
                }
            } else {
                std::cout << "[TxClient] Query ejecutada con éxito OK.\n";
            }
        } else {
            std::cout << "[TxClient] Query ejecutada con éxito OK.\n";
        }
        return true;
    } else if (res.opcode == protocol::Opcode::TX_ABORTED_DEADLOCK) {
        protocol::TransactionPayload payload{};
        socket_->recv_all(&payload, sizeof(payload));
        std::cout << "\n[!] ALERTA DE SEGURIDAD Y CONCURRENCIA [!]\n";
        std::cout << "    Transacción #" << payload.tx_id << " abortada automáticamente por el TxKernel.\n";
        std::cout << "    Razón: DEADLOCK DETECTADO (Abrazo Mortal resuelto por el Orquestador).\n\n";
        in_transaction_ = false;
        current_tx_id_ = 0;
        return true;
    } else {
        std::cout << "[TxClient] Error en la ejecución de la consulta.\n";
        return true;
    }
}

} // namespace txdb::client
