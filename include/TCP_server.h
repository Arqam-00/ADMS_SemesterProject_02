#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include "RaftNode.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <thread>

class TCPServer {
private:
    RaftNode& raft;
    int client_fd;
    int raft_fd;
    bool running;

    void handleRaft(int client_fd) {
        char buffer[4096];
        while (running) {
            memset(buffer, 0, sizeof(buffer));
            int n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;

            char msg_type = buffer[0];
            if (msg_type == 'V') {
                std::vector<char> bin_data(buffer, buffer + n);
                RequestVote rpc = RequestVote::deserialize(bin_data);
                RequestVoteResponse voteResp = raft.handleRequestVote(rpc);
                auto resp_data = voteResp.serialize();
                write(client_fd, resp_data.data(), resp_data.size());
            }
            else if (msg_type == 'A') {
                std::vector<char> bin_data(buffer, buffer + n);
                AppendEntries rpc = AppendEntries::deserialize(bin_data);
                AppendEntriesResponse aeResp = raft.handleAppendEntries(rpc);
                auto resp_data = aeResp.serialize();
                write(client_fd, resp_data.data(), resp_data.size());
            }
        }
        close(client_fd);
    }

    void handleClient(int client_fd) {
        char buffer[4096];
        while (running) {
            memset(buffer, 0, sizeof(buffer));
            int n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;

            char msg_type = buffer[0];
            std::string response = "";
            try {
                if (msg_type == 'Q') {
                    response = "";
                    break;
                }
                else if (msg_type == 'S') {
                    response = raft.getStatus();
                }
                else if (msg_type == 'G') {
                    std::string key(buffer + 1, buffer + n);
                    std::string val = raft.get(key);
                    response = val.empty() ? "(null)\n" : val + "\n";
                }
                else if (msg_type == 'C') {
                    std::vector<char> bin_data(buffer + 1, buffer + n);
                    Command cmd = Command::deserialize(bin_data);
                    raft.submit(cmd);
                    response = "OK\n";
                }
                else {
                    response = "ERROR: Unknown Protocol\n";
                }
            }
            catch (const std::runtime_error& e) {
                if (std::string(e.what()) == "NOT_LEADER") {
                    int leader_id = raft.getLeaderId();
                    if (leader_id != 0) {
                        response = "NOT_LEADER leader_id=" + std::to_string(leader_id) + "\n";
                    } else {
                        response = "NOT_LEADER unknown\n";
                    }
                } else {
                    response = "ERROR: " + std::string(e.what()) + "\n";
                }
            }
            write(client_fd, response.c_str(), response.size());
        }
        close(client_fd);
    }

public:
    TCPServer(RaftNode& r, int client_port, int raft_port) : raft(r), running(true) {
        
        // Client socket
        client_fd = socket(AF_INET, SOCK_STREAM, 0);
        int opt = 1;
        setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(client_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        bind(client_fd, (struct sockaddr*)&addr, sizeof(addr));
        listen(client_fd, 5);

        // Raft socket
        raft_fd = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(raft_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        addr.sin_port = htons(raft_port);
        bind(raft_fd, (struct sockaddr*)&addr, sizeof(addr));
        listen(raft_fd, 5);

        std::cout << "Client server ready on port " << client_port << std::endl;
        std::cout << "Raft server ready on port " << raft_port << std::endl;
    }

    ~TCPServer() {
        running = false;
        close(client_fd);
        close(raft_fd);
    }

    void RunClient() {
        while (running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int fd = accept(client_fd, (struct sockaddr*)&client_addr, &client_len);
            if (fd >= 0) {
                std::thread(&TCPServer::handleClient, this, fd).detach();
            }
        }
    }

    void RunRaft() {
        while (running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int fd = accept(raft_fd, (struct sockaddr*)&client_addr, &client_len);
            if (fd >= 0) {
                std::thread(&TCPServer::handleRaft, this, fd).detach();
            }
        }
    }
};

#endif