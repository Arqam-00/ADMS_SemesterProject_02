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
    int server_fd;
    bool running;

    void handleClient(int client_fd) {
        char buffer[4096];
        while (running) {
            memset(buffer, 0, sizeof(buffer));
            int n = read(client_fd, buffer, sizeof(buffer) - 1);
            if (n <= 0) break;

            char msg_type = buffer[0];
            std::string response = "";

            if (msg_type == 'Q') {
                break;
            }
            else if (msg_type == 'S') {
                response = raft.getStatus();
                write(client_fd, response.c_str(), response.size());
            }
            else if (msg_type == 'G') {
                std::string key(buffer + 1, buffer + n - 1);
                std::string val = raft.get(key);
                response = val.empty() ? "(null)\n" : val + "\n";
                write(client_fd, response.c_str(), response.size());
            }
            else if (msg_type == 'C') {
                try {
                    std::vector<char> bin_data(buffer + 1, buffer + n);
                    Command cmd = Command::deserialize(bin_data);
                    raft.submit(cmd);
                    response = "OK\n";
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
            else if (msg_type == 'V') {
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
            else {
                response = "ERROR: Unknown Protocol\n";
                write(client_fd, response.c_str(), response.size());
            }
        }
        close(client_fd);
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
        std::cout << "Server ready for client commands and peer RPCs..." << std::endl;
        while (running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd >= 0) {
                std::thread(&TCPServer::handleClient, this, client_fd).detach();
            }
        }
    }
};

#endif