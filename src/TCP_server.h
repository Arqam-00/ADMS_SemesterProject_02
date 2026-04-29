#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "RaftNode.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <thread>
#include <sstream>
#include <algorithm>
#include <iostream>
//made it as a class so we can make multiple objects for different servers in multiple mains
class TCPServer {
private:
    RaftNode& raft;
    int server_fd;
    bool running;
    
    void handleClient(int client_fd) {
        char buffer[4096];
        while (running) {
            memset(buffer, 0, sizeof(buffer));
            int n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;
            std::string request(buffer);
            request.erase(request.find_last_not_of("\n\r") + 1);
            std::string response = processCommand(request);
            response += "\n";
            write(client_fd, response.c_str(), response.size());
            if (request == "QUIT" || request == "quit" || request == "Quit") break;
        }
        close(client_fd);
        std::cout << "Client disconnected" << std::endl;
    }
    
    std::string processCommand(const std::string& cmd) {
        std::istringstream iss(cmd);
        std::string op;
        iss >> op;
        std::transform(op.begin(), op.end(), op.begin(), ::toupper);
        if (op == "GET") {
            std::string key;
            iss >> key;
            if (key.empty()) return "ERROR: GET requires key";
            std::string val = raft.get(key);
            return val.empty() ? "NULL" : val;
        }
        else if (op == "PUT") {
            std::string key, value;
            iss >> key >> value;
            if (key.empty() || value.empty()) return "ERROR: PUT requires key and value";
            try {
                raft.submit(cmd);
                return "OK";
            } catch (const std::exception& e) {
                return "ERROR: " + std::string(e.what());
            }
        }
        else if (op == "DELETE") {
            std::string key;
            iss >> key;
            if (key.empty()) return "ERROR: DELETE requires key";
            try {
                raft.submit(cmd);
                return "OK";
            } catch (const std::exception& e) {
                return "ERROR: " + std::string(e.what());
            }
        }
        else if (op == "STATUS") {
            return raft.getStatus();
        }
        else if (op == "QUIT") {
            return "Goodbye";
        }
        else {
            return "ERROR: Unknown command. Commands: PUT, GET, DELETE, STATUS, QUIT";
        }
    }
    
public:
    TCPServer(RaftNode& r, int port) : raft(r), running(true) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));
        listen(server_fd, 5);
    }
    
    ~TCPServer() {
        running = false;
        close(server_fd);
    }
    
    void run() {
        std::cout << "Server listening on port 8080" << std::endl;
        while (running) {
            int client_fd = accept(server_fd, nullptr, nullptr);
            if (client_fd >= 0) {
                std::cout << "Client connected!" << std::endl;
                std::thread([this, client_fd]() { handleClient(client_fd); }).detach();
            }
        }
    }
};

#endif