#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

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
        
        if (cmd == "QUIT\n") break;
    }
    
    close(sock);
    return 0;
}