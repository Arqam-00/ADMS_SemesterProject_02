#ifndef PEER_H
#define PEER_H

#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>

class Peer {
private:
    // Peer's Data
    int id;
    std::string ip;
    int port;
    // For Connection State
    int sock_fd;
    bool connected;
    bool running;
    //Threading
    std::thread connector_thread;
    std::mutex send_mutex;

    void connectLoop() {
        while (running) {
            if (!connected) { // agar connect nhi hai to peer k sath connection dubara try karo
                sock_fd = socket(AF_INET, SOCK_STREAM, 0);
                
                // Set a 50ms read timeout so waiting for RPC responses doesn't freeze the node
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 50000; 
                setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

                if (connect(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                    connected = true;
                } else {
                    close(sock_fd);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

public:
    Peer(int peer_id, const std::string& peer_ip, int peer_port) 
        : id(peer_id), ip(peer_ip), port(peer_port), sock_fd(-1), connected(false), running(true) {
            // makes a thread for this object to run
        connector_thread = std::thread(&Peer::connectLoop, this);
    }

    ~Peer() {
        running = false;
        if (connector_thread.joinable()) connector_thread.join();
        if (sock_fd >= 0) close(sock_fd);
    }

    int getId() const { return id; }
    //sends an RPC and gets back the reply
    // This now waits for and returns the response from the other server!
    std::vector<char> sendRPC(const std::vector<char>& data) {
        // locking the thread so same socket is reserved for this thread only
        std::lock_guard<std::mutex> lock(send_mutex);
        if (connected) {
            if (send(sock_fd, data.data(), data.size(), 0) < 0) {
                connected = false;
                close(sock_fd);
                sock_fd = -1;
                return {};
            }
            
            // Wait for the vote/heartbeat response
            char buffer[4096];
            memset(buffer, 0, sizeof(buffer));
            int n = recv(sock_fd, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                return std::vector<char>(buffer, buffer + n);
            }
        }
        return {};
    }
};

#endif