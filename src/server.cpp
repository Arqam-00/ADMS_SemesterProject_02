    #include <iostream>
    #include <string>
    #include <vector>
    #include <sstream>
    #include <memory>
    #include <sys/stat.h>
    #include <unistd.h>
    #include "../include/TCP_server.h"
    #include "../include/Peer.h"

    int main(int argc, char* argv[]) {
        int client_port = 6001;
        int raft_port = 7001;

        int node_id = 1; 
        std::string data_dir = ".";
        std::vector<std::shared_ptr<Peer>> peers;

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];
            
            if (arg == "--id" && i + 1 < argc) {
                node_id = std::stoi(argv[++i]);
            }
            else if (arg == "--port" && i + 1 < argc) {
                client_port = std::stoi(argv[++i]);
            }
            else if (arg == "--raft-port" && i + 1 < argc) {
                raft_port = std::stoi(argv[++i]);
            }
            else if (arg == "--peers" && i + 1 < argc) {
                std::string peers_str = argv[++i];
                std::stringstream ss(peers_str);
                std::string token;
                
                while (std::getline(ss, token, ',')) {
                    size_t at_pos = token.find('@');
                    size_t colon_pos = token.find(':', at_pos);
                    if (at_pos != std::string::npos && colon_pos != std::string::npos) {
                        int p_id = std::stoi(token.substr(0, at_pos));
                        std::string p_ip = token.substr(at_pos + 1, colon_pos - at_pos - 1);
                        int p_port = std::stoi(token.substr(colon_pos + 1));
                        
                        peers.push_back(std::make_shared<Peer>(p_id, p_ip, p_port));
                    }
                }
            }
            else if (arg == "--data" && i + 1 < argc) {
                data_dir = argv[++i];
                mkdir(data_dir.c_str(), 0755);
                chdir(data_dir.c_str());
            }
            else if (arg == "-port" && i + 1 < argc) {
                client_port = std::stoi(argv[++i]);
            }
            else if (isdigit(arg[0])) {
                node_id = std::stoi(arg);
            }
        }

        std::cout << "[Node " << node_id << "] Starting..." << std::endl;
        std::cout << "[Node " << node_id << "] Tracking " << peers.size() << " peers." << std::endl;
        
        RaftNode raft(node_id, peers);
        TCPServer server(raft, client_port, raft_port);

        std::thread client_thread(&TCPServer::RunClient, &server);
        std::thread raft_thread(&TCPServer::RunRaft, &server);

        raft.startBackground();


        client_thread.join();
        raft_thread.join();
        return 0;
    }