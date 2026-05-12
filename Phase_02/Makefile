CXX = g++
CXXFLAGS = -std=c++11 -Wall -pthread

all: raftkvid raftkv-cli

raftkvid: src/server.cpp
	$(CXX) $(CXXFLAGS) src/server.cpp -o raftkvid

raftkv-cli: src/client.cpp
	$(CXX) $(CXXFLAGS) src/client.cpp -o raftkv-cli

clean:
	rm -f raftkvid raftkv-cli raft.log raft.meta raft.meta.tmp