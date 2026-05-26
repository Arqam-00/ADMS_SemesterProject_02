#ifndef RAFT_NODE_H
#define RAFT_NODE_H

#include "Storage.h"
#include "StateMachine.h"
#include "Peer.h"
#include "RPC.h"
#include <sstream>
#include <algorithm>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <iostream>

enum class NodeState {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

//The main Raft node , now with brand new elections
class RaftNode {
private:
    Storage store;
    StateMachine sm;
    
    // Cluster membership
    int node_id;
    std::vector<std::shared_ptr<Peer>> peers;
    
    // Threading and state
    mutable std::mutex mtx;
    NodeState state;
    bool running;
    std::chrono::time_point<std::chrono::steady_clock> last_heartbeat;
    int current_timeout;
    std::thread background_thread;
    
    //Replay log on startup to recover from crash
    void replay() {
        std::cout << "============== A Raft node has been initiated ===================\n"; 
        auto entries = store.readAll();
    
        //state machine rebuild 
        for (auto& entry : entries) { sm.apply(entry.getCommand()); }
        //Updated lastApplied to last entry index
        if (!entries.empty()) {
            store.setApplied(entries.back().index);
            store.setCommit(entries.back().index);
        }
        //i know this is bad algorithm but we are not allowed to make a backup are we
    }
    
    //  Generate random timeout
    int getRandomTimeout() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(300, 500);
        return dist(gen);
    }
    
    // Background election/heartbeat loop
    void backgroundLoop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            std::unique_lock<std::mutex> lock(mtx);
            
            if (state == NodeState::LEADER) {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count();
                
                if (elapsed >= 100) {
                    last_heartbeat = now;
                    AppendEntries heartbeat;
                    heartbeat.term = store.getTerm();
                    heartbeat.leaderId = node_id;
                    heartbeat.prevLogIndex = store.logLen();
                    heartbeat.prevLogTerm = 0; 
                    heartbeat.leaderCommit = store.getCommit();
                    
                    auto payload = heartbeat.serialize();
                    
                    lock.unlock();
                    for (auto& peer : peers) {
                        peer->sendRPC(payload);
                    }
                    lock.lock();
                }
            } else {
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count();
                
                if (elapsed >= current_timeout) {
                    startElection(lock);
                }
            }
        }
    }
    
    void startElection(std::unique_lock<std::mutex>& lock) {
        state = NodeState::CANDIDATE;
        
        if (store.getTerm() > 1000000) store.setTerm(0);
        
        store.setTerm(store.getTerm() + 1);
        store.setVoted(std::to_string(node_id)); 
        last_heartbeat = std::chrono::steady_clock::now(); 
        current_timeout = getRandomTimeout();
        
        std::cout << "\n[Node " << node_id << "] Election timeout! Converting to CANDIDATE.\n";
        std::cout << "[Node " << node_id << "] Starting election for term " << store.getTerm() << "\n";
        
        RequestVote rpc;
        rpc.term = store.getTerm();
        rpc.candidateId = node_id;
        rpc.lastLogIndex = store.logLen();
        rpc.lastLogTerm = 0; 
        
        auto payload = rpc.serialize();
        int votes = 1;
        
        lock.unlock();
        
        for (auto& peer : peers) {
            std::vector<char> resp_data = peer->sendRPC(payload);
            if (!resp_data.empty() && resp_data[0] == 'W') {
                
                RequestVoteResponse resp = RequestVoteResponse::deserialize(resp_data);
                
                if (resp.voteGranted) {
                    votes++;
                }
            }
        }
        
        lock.lock();
        
        if (votes >= 3 && state == NodeState::CANDIDATE) {
            std::cout << "[Node " << node_id << "] Won election with " << votes << " votes!\n";
            becomeLeader();
        } else {
            std::cout << "[Node " << node_id << "] Election finished with " << votes << " votes. Waiting...\n";
        }
    }
    
public:
    RaftNode(int id, std::vector<std::shared_ptr<Peer>> cluster_peers) 
        : node_id(id), peers(cluster_peers), state(NodeState::FOLLOWER), running(true) {
        replay();
        last_heartbeat = std::chrono::steady_clock::now();
        current_timeout = getRandomTimeout();
        background_thread = std::thread(&RaftNode::backgroundLoop, this);
    }
    
    ~RaftNode() {
        running = false;
        if (background_thread.joinable()) {
            background_thread.join();
        }
    }
    
    // Accept a command from client
    void submit(const Command& cmd) {
        std::lock_guard<std::mutex> lock(mtx);
        if (state != NodeState::LEADER) {
            throw std::runtime_error("NOT_LEADER");
        }
        
        uint64_t new_idx = store.logLen() + 1;
        LogEntry entry(store.getTerm(), new_idx, cmd);
        store.append(entry);
        
        store.setCommit(new_idx);
        sm.apply(cmd);
        store.setApplied(new_idx);
    }
    
    //const and thread-safe
    std::string get(const std::string& key) const {
        std::lock_guard<std::mutex> lock(mtx);
        return sm.get(key);
    }
    
    std::string getStatus() const {
        std::lock_guard<std::mutex> lock(mtx);
        std::ostringstream oss;
        oss << "node_id:\n" << node_id << "\n";
        oss << "state:\n" << (state == NodeState::LEADER ? "LEADER" : (state == NodeState::CANDIDATE ? "CANDIDATE" : "FOLLOWER")) << "\n";
        oss << "term:\n" << store.getTerm() << "\n";
        oss << "log length:\n" << store.logLen() << "\n";
        oss << "commitIndex:\n" << store.getCommit() << "\n";
        return oss.str();
    }
    
    // NEW: Handle vote requests
    RequestVoteResponse handleRequestVote(const RequestVote& rpc) {
        std::lock_guard<std::mutex> lock(mtx);
        RequestVoteResponse resp;
        resp.term = store.getTerm();
        resp.voteGranted = false;

        if (rpc.term < store.getTerm()) return resp;

        if (rpc.term > store.getTerm()) {
            store.setTerm(rpc.term);
            state = NodeState::FOLLOWER;
            store.setVoted(""); 
            resp.term = rpc.term;
        }

        std::string votedFor = store.getVoted();
        if (votedFor == "" || votedFor == std::to_string(rpc.candidateId)) {
            store.setVoted(std::to_string(rpc.candidateId));
            resp.voteGranted = true;
            last_heartbeat = std::chrono::steady_clock::now(); 
            current_timeout = getRandomTimeout();
            std::cout << "[Node " << node_id << "] Voted for Node " << rpc.candidateId << " in term " << rpc.term << "\n";
        }
        return resp;
    }
    
    // NEW: Handle heartbeats
    AppendEntriesResponse handleAppendEntries(const AppendEntries& rpc) {
        std::lock_guard<std::mutex> lock(mtx);
        AppendEntriesResponse resp;
        resp.term = store.getTerm();
        resp.success = false;

        if (rpc.term < store.getTerm()) return resp;

        if (rpc.term >= store.getTerm()) {
            store.setTerm(rpc.term);
            if (state != NodeState::FOLLOWER) {
                std::cout << "[Node " << node_id << "] Discovered Leader " << rpc.leaderId << ", stepping down to FOLLOWER.\n";
            }
            state = NodeState::FOLLOWER;
            last_heartbeat = std::chrono::steady_clock::now(); 
            current_timeout = getRandomTimeout();
            resp.term = rpc.term;
            resp.success = true; 
        }
        return resp;
    }
    
    // NEW: Become leader
    void becomeLeader() {
        if (state == NodeState::CANDIDATE) {
            state = NodeState::LEADER;
            std::cout << "\n*** [Node " << node_id << "] IS NOW THE LEADER FOR TERM " << store.getTerm() << " ***\n\n";
        }
    }
};

#endif