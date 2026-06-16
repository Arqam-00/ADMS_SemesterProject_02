#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <thread>
#include "../include/Command.h"

int connect_to_server(int port) {
    std::string ip = "127.0.0.1";
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        return -1;
    }
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }
    return sock;
}

int main(int argc, char* argv[]) {
    std::string ip = "127.0.0.1";
    int port = 6001;

    // Expected format: ./raftkv-cli <host> <port>
    // Example: ./raftkv-cli h1 6001
    if (argc >= 3) {
        port = std::stoi(argv[2]);
        // For local testing we ignore the host string ('h1') and use localhost
    }

    int sock = connect_to_server(port);
    if (sock < 0) {
        std::cerr << "Cannot connect to server on port " << port << std::endl;
        return 1;
    }

    if (isatty(STDIN_FILENO)) {
        std::cout << "Connected to Raft server. Type commands (PUT <key> <val>, GET <key>, DELETE <key>, \\status, QUIT)" << std::endl;
    }
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
        size_t retry = 3;
        while(retry--){
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
                std::string response(buffer);
            
                if (response.find("NOT_LEADER leader_id=") != std::string::npos) {
                    size_t pos = response.find("leader_id=");
                    if (pos != std::string::npos) {
                        std::string leader_id_str = response.substr(pos + 10);
                        // Trim whitespace/newline
                        leader_id_str.erase(leader_id_str.find_last_not_of(" \n\r\t") + 1);
                        int leader_id = std::stoi(leader_id_str);
                        int new_port = 6000 + leader_id; // Client ports are 6001-6005
                        std::cout << "Redirecting to leader node " << leader_id << " on port " << new_port << std::endl;
                    
                        close(sock);
                        sock = connect_to_server(new_port);
                        if (sock < 0) {
                            std::cerr << "Failed to connect to leader on port " << new_port << std::endl;
                            return 1;
                        }
                        continue;
                        //std::cout << "Reconnected to leader.Please retry!" << std::endl;
                    
                    }
                } else if (response.find("NOT_LEADER unknown") != std::string::npos) {
                    std::cout << "Leader unknown, retry later!" << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    std::cout << "Retrying!"<<std::endl;

                } else {
                    std::cout << response;
                    retry = 0;
                }
            } else if (n == 0) {
                std::cout << "Server closed connection." << std::endl;
                break;
            } else {
                std::cerr << "Receive error" << std::endl;
                break;
            }

        }
    }

    close(sock);
    return 0;
}