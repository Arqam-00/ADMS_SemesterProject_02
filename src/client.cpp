#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include "../include/Command.h"

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    int port = 8080;

    // Expected format: ./raftkv-cli <host> <port>
    // Example: ./raftkv-cli h1 6001
    if (argc >= 3) {
        port = std::stoi(argv[2]);
        // For local testing we ignore the host string ('h1') and use localhost
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Cannot connect to server on port " << port << std::endl;
        close(sock);
        return 1;
    }

    std::cout << "Connected to Raft server. Type commands (PUT <key> <val>, GET <key>, DELETE <key>, \\status, QUIT)" << std::endl;

    std::string line;
    while (true) {
        std::cout << "> ";
        if (!std::getline(std::cin, line)) break;
        if (line.empty()) continue;

        std::string op;
        std::istringstream iss(line);
        iss >> op;
        std::transform(op.begin(), op.end(), op.begin(), ::toupper);

        std::vector<char> packet;
        std::string key, val;

        if (op == "\\STATUS") {
            packet.push_back('S');
        }
        else if (op == "QUIT") {
            packet.push_back('Q');
            if (send(sock, packet.data(), packet.size(), 0) < 0) {
                std::cerr << "Send failed" << std::endl;
            }
            break;
        }
        else if (op == "GET") {
            packet.push_back('G');
            iss >> key;
            packet.insert(packet.end(), key.begin(), key.end());
        }
        else {
            // PUT or DELETE
            packet.push_back('C');
            Command cmd;
            if (op == "PUT") {
                iss >> key >> val;
                cmd = Command(OpType::PUT, key, val);
            }
            else if (op == "DELETE") {
                iss >> key;
                cmd = Command(OpType::DELETE, key);
            }
            else {
                std::cout << "Unknown command." << std::endl;
                continue;
            }
            auto bin_cmd = cmd.serialize();
            packet.insert(packet.end(), bin_cmd.begin(), bin_cmd.end());
        }

        // Send the packet
        if (send(sock, packet.data(), packet.size(), 0) < 0) {
            std::cerr << "Send failed" << std::endl;
            break;
        }

        // Read response (server may send multiple lines)
        char buffer[4096];
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            std::cout << buffer;
        } else if (n == 0) {
            std::cout << "Server closed connection." << std::endl;
            break;
        } else {
            std::cerr << "Receive error" << std::endl;
            break;
        }
    }

    close(sock);
    return 0;
}