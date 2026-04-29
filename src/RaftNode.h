#ifndef RAFT_NODE_H
#define RAFT_NODE_H

#include "Storage.h"
#include "StateMachine.h"
#include <sstream>
#include <algorithm>

//The main Raft node (no elections yet)
class RaftNode {
private:
    Storage store;
    StateMachine sm;
    
    //Replay log on startup to recover from crash
    void replay() {
        std::cout << "============== A Raft node has been initiated ===================\n"; 
        auto entries = store.readAll();
    
        //state machine rebuild
        for (auto& entry : entries) { sm.apply(entry.getCommand()); }
        //Updated lastApplied to last entry index
        if (!entries.empty()) {
            store.setApplied(entries.back().index);
        }
        //i know this is bad algorithm but we are not allowed to make a backup are we
    }
    
public:
    RaftNode() {
        replay();
    }
    
    // Accept a command from client
    void submit(const std::string& line) {
        std::string op, key, value;
        std::istringstream iss(line);
        iss >> op;
        
        //As Sir abd e muneeb once said "User is an idiot so we need to handle both "PUT" or "put"
        std::transform(op.begin(), op.end(), op.begin(), ::toupper);
        Command cmd;
        if (op == "PUT") {
            iss >> key >> value;
            cmd = Command(OpType::PUT, key, value);
        } else if (op == "DELETE") {
            iss >> key;
            cmd = Command(OpType::DELETE, key);
        } else {
            throw std::runtime_error("Unknown command: " + op);
        }
        
        uint64_t new_idx = store.logLen() + 1;
        LogEntry entry(0, new_idx, cmd);
        store.append(entry);
        store.setCommit(new_idx);
        sm.apply(cmd);
        store.setApplied(new_idx);
    }
    
    std::string get(const std::string& key) { return sm.get(key); }
    
    std::string getStatus() {
        std::ostringstream oss;
        oss << "currentTerm: " << store.getTerm() << "\n"
            << "logLength: " << store.logLen() << "\n"
            << "commitIndex: " << store.getCommit();
        return oss.str();
    }
};

#endif