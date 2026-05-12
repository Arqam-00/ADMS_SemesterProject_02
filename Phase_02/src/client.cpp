#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
<<<<<<< HEAD

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);//ipv4,TCP,automatic protocol
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Cannot connect to server" << std::endl;
        return 1;
    }
    
    std::cout << "Connected to Raft server. Type commands (PUT, GET, DELETE, STATUS, QUIT)" << std::endl;
    
    std::string cmd;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, cmd);
        
        if (cmd.empty()) continue;
        cmd += "\n";
        send(sock, cmd.c_str(), cmd.size(), 0);
        
        //Response from server
        char buffer[4096];
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            std::cout << buffer;
        }
        
        if (cmd == "QUIT\n"|| cmd == "quit\n" || cmd == "Quit\n") break;
    }
    
=======
#include <sstream>
#include <algorithm>
#include "../include/Command.h"

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    int port = 8080;

    // Expected format: ./raftkv-cli h1 6001
    if (argc >= 3) {
        port = std::stoi(argv[2]);
        // For local testing, we ignore the host string (e.g., 'h1') and just use localhost
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Cannot connect to server on port " << port << std::endl;
        return 1;
    }

    std::cout << "Connected to Raft server. Type commands (PUT <key> <val>, GET <key>, DELETE <key>, \\status, QUIT)" << std::endl;

    std::string line;
    while (true) {
        std::cout << "> ";
        std::getline(std::cin, line);
        if (line.empty()) continue;

        std::string op;
        std::istringstream iss(line);
        iss >> op;
        std::transform(op.begin(), op.end(), op.begin(), ::toupper);

        std::vector<char> packet;
        std::string key, val; // <-- Move this declaration up here!

        if (op == "\\STATUS") {
            packet.push_back('S');
        }
        else if (op == "QUIT") {
            packet.push_back('Q');
            send(sock, packet.data(), packet.size(), 0);
            break;
        }
        else if (op == "GET") {
            // Send a dedicated 'G' packet with just the key text
            packet.push_back('G');
            iss >> key;
            packet.insert(packet.end(), key.begin(), key.end());
        }
        else {
            // It's a PUT or DELETE, serialize it to binary
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

        send(sock, packet.data(), packet.size(), 0);

        char buffer[4096];
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            std::cout << buffer;
        }
    }
>>>>>>> 70aea4a (Phase_02 Completed)
    close(sock);
    return 0;
}