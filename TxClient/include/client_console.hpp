#pragma once

#include <string>
#include <memory>
#include "network.hpp"
#include "protocol.hpp"

namespace txdb::client {

class ClientConsole {
public:
    ClientConsole();
    ~ClientConsole();

    bool connect(const std::string& host = "127.0.0.1", uint16_t port = 9001);
    void disconnect();
    bool is_connected() const;

    void run_repl();
    bool execute_command(const std::string& line);

private:
    std::string trim(const std::string& str) const;
    void print_banner() const;

    std::unique_ptr<txdb::network::Socket> socket_;
    uint32_t current_tx_id_{0};
    bool in_transaction_{false};
};

} // namespace txdb::client
