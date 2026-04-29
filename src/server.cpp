#include <iostream>
#include "TCP_server.h"

int main() {
    RaftNode raft;
    TCPServer server(raft, 8080);
    server.run();
    return 0;
}