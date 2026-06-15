    #ifndef PEER_H
    #define PEER_H

    #include <string>
    #include <vector>
    #include <thread>
    #include <mutex>
    #include <condition_variable>
    #include <atomic>
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cstring>
    #include <iostream>

    class Peer {
    private:
        int id;
        std::string ip;
        int port;
        int sock_fd;
        std::atomic<bool> connected;
        std::atomic<bool> running;
        std::thread bg_thread;
        mutable std::mutex mtx;
        std::condition_variable cv;

        void backgroundConnect() {
            while (running) {
                if (!connected) {
                    int fd = socket(AF_INET, SOCK_STREAM, 0);
                    if (fd >= 0) {
                        // 1 second timeout for connect, send, recv
                        struct timeval tv;
                        tv.tv_sec = 1;
                        tv.tv_usec = 0;
                        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
                        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

                        struct sockaddr_in addr;
                        addr.sin_family = AF_INET;
                        addr.sin_port = htons(port);
                        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

                        if (::connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                            std::lock_guard<std::mutex> lock(mtx);
                            if (sock_fd >= 0) close(sock_fd);
                            sock_fd = fd;
                            connected = true;
                            cv.notify_all();   // wake any waiting sendRPC
                            continue;
                        }
                        close(fd);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(300));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }

    public:
        Peer(int peer_id, const std::string& peer_ip, int peer_port)
            : id(peer_id), ip(peer_ip), port(peer_port), sock_fd(-1), connected(false), running(true) {
            bg_thread = std::thread(&Peer::backgroundConnect, this);
        }

        ~Peer() {
            running = false;
            if (bg_thread.joinable()) bg_thread.join();
            std::lock_guard<std::mutex> lock(mtx);
            if (sock_fd >= 0) close(sock_fd);
        }

        int getId() const { return id; }

        std::vector<char> sendRPC(const std::vector<char>& request) {
            std::unique_lock<std::mutex> lock(mtx);
            // Wait up to 1second for background thread to connect
            if (!connected) {
                cv.wait_for(lock, std::chrono::seconds(1), [this]{ return connected.load(); });
            }
            if (!connected) return {};

            if (send(sock_fd, request.data(), request.size(), 0) < 0) {
                connected = false;
                close(sock_fd);
                sock_fd = -1;
                return {};
            }

            char buffer[65536];
            int n = recv(sock_fd, buffer, sizeof(buffer), 0);
            if (n > 0) {
                return std::vector<char>(buffer, buffer + n);
            }
            connected = false;
            close(sock_fd);
            sock_fd = -1;
            return {};
        }
    };

    #endif