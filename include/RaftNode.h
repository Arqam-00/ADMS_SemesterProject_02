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

enum class NodeState { FOLLOWER, CANDIDATE, LEADER };

class RaftNode {
private:
    Storage store;
    StateMachine sm;

    int node_id;
    std::vector<std::shared_ptr<Peer>> peers;

    mutable std::mutex mtx;
    NodeState state;
    bool running;
    std::chrono::time_point<std::chrono::steady_clock> last_heartbeat;
    int current_timeout;
    std::thread background_thread;
    int current_leader_id;   // will be 0 if unknown(start)

    std::vector<uint64_t> nextIndex;
    std::vector<uint64_t> matchIndex;

    void replay() {
        std::cout << "============== Raft node " << node_id << " initiated ===================\n";
        auto entries = store.readAll();
        for (auto& entry : entries) {
            sm.apply(entry.getCommand());
        }
        if (!entries.empty()) {
            store.setApplied(entries.back().index);
            store.setCommit(entries.back().index);
        }
    }

    int getRandomTimeout() {
        return 3000 + rand() % 201;
    }

    void startElection(std::unique_lock<std::mutex>& lock) {
        state = NodeState::CANDIDATE;
        store.setTerm(store.getTerm() + 1);
        store.setVoted(std::to_string(node_id));
        last_heartbeat = std::chrono::steady_clock::now();
        current_timeout = getRandomTimeout();

        std::cout << "\n[Node " << node_id << "] Election timeout! Starting election for term " << store.getTerm() << "\n";

        RequestVote rpc;
        rpc.term = store.getTerm();
        rpc.candidateId = node_id;
        rpc.lastLogIndex = store.getLastLogIndex();
        rpc.lastLogTerm = store.getLastLogTerm();

        auto payload = rpc.serialize();
        int votes = 1;

        lock.unlock();
        for (auto& peer : peers) {
            std::vector<char> resp_data = peer->sendRPC(payload);

            //debug
            if (resp_data.empty()) {
                    std::cerr << "[Node " << node_id << "] WARNING: Peer " << peer->getId() << " returned empty response!" << std::endl;
            } else if (resp_data[0] != 'W') {
                std::cerr << "[Node " << node_id << "] WARNING: Peer " << peer->getId() << " returned bad type: " << (int)resp_data[0] << std::endl;
            }
            //Debug

            if (!resp_data.empty() && resp_data[0] == 'W') {
                RequestVoteResponse resp = RequestVoteResponse::deserialize(resp_data);
                if (resp.term > store.getTerm()) {
                    lock.lock();
                    if (resp.term > store.getTerm()) {
                        store.setTerm(resp.term);
                        state = NodeState::FOLLOWER;
                        store.setVoted("");
                        last_heartbeat = std::chrono::steady_clock::now();
                        current_timeout = getRandomTimeout();
                        lock.unlock();
                        return;
                    }
                    lock.unlock();
                } else if (resp.voteGranted) {
                    votes++;
                }
            }
        }
        lock.lock();

        int majority = (peers.size() + 1) / 2 + 1;
        if (votes >= majority && state == NodeState::CANDIDATE) {
            becomeLeader();
        } else {
            std::cout << "[Node " << node_id << "] Election finished with " << votes << " votes (need " << majority << ").\n";
        }
    }

    void becomeLeader() {
        if (state != NodeState::CANDIDATE) return;
        state = NodeState::LEADER;
        current_leader_id = node_id;   // I am the leader
        uint64_t last = store.getLastLogIndex();
        nextIndex.assign(peers.size(), last + 1);
        matchIndex.assign(peers.size(), 0);
        Command noop(OpType::NOOP);
        uint64_t new_idx = store.logLen() + 1;
        LogEntry entry(store.getTerm(), new_idx, noop);
        store.append(entry);
        std::cout << "\n*** [Node " << node_id << "] IS NOW LEADER FOR TERM " << store.getTerm() << " ***\n";
    }

    void sendAppendEntries(size_t peer_idx) {
        auto& peer = peers[peer_idx];
        AppendEntries ae;
        ae.term = store.getTerm();
        ae.leaderId = node_id;
        ae.leaderCommit = store.getCommit();
        ae.prevLogIndex = nextIndex[peer_idx] - 1;
        ae.prevLogTerm = (ae.prevLogIndex == 0) ? 0 : store.getTermAt(ae.prevLogIndex);
        if (nextIndex[peer_idx] <= store.getLastLogIndex()) {
            auto entries = store.readFrom(nextIndex[peer_idx]);
            for (auto& e : entries) ae.entries.push_back(e);
        }
        auto payload = ae.serialize();
        std::vector<char> resp_data = peer->sendRPC(payload);
        if (!resp_data.empty() && resp_data[0] == 'R') {
            AppendEntriesResponse resp = AppendEntriesResponse::deserialize(resp_data);
            if (resp.term > store.getTerm()) {
                std::lock_guard<std::mutex> lock(mtx);
                store.setTerm(resp.term);
                state = NodeState::FOLLOWER;
                store.setVoted("");
                return;
            } else if (resp.success && !ae.entries.empty()) {
                uint64_t last_sent = ae.prevLogIndex + ae.entries.size();
                matchIndex[peer_idx] = std::max(matchIndex[peer_idx], last_sent);
                nextIndex[peer_idx] = matchIndex[peer_idx] + 1;
                advanceCommitIndex();
            } else {
                if (nextIndex[peer_idx] > 1) nextIndex[peer_idx]--;
            }
        } else {
            if (nextIndex[peer_idx] > 1) nextIndex[peer_idx]--;
        }
    }

    void advanceCommitIndex() {
        uint64_t new_commit = store.getCommit();
        for (uint64_t idx = new_commit + 1; idx <= store.getLastLogIndex(); ++idx) {
            if (store.getTermAt(idx) != store.getTerm()) continue;
            int count = 1;
            for (size_t j = 0; j < peers.size(); ++j) {
                if (matchIndex[j] >= idx) count++;
            }
            if (count >= (peers.size()+1)/2 + 1) {
                new_commit = idx;
            } else break;
        }
        if (new_commit > store.getCommit()) {
            store.setCommit(new_commit);
            applyCommitted();
        }
    }

    void applyCommitted() {
        uint64_t last_applied = store.getApplied();
        uint64_t commit = store.getCommit();
        if (commit > last_applied) {
            auto entries = store.readRange(last_applied + 1, commit);
            for (auto& e : entries) {
                sm.apply(e.getCommand());
            }
            store.setApplied(commit);
        }
    }

    void backgroundLoop() {
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::unique_lock<std::mutex> lock(mtx);

            if (state == NodeState::LEADER) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count() >= 200) {
                    last_heartbeat = now;
                    for (size_t i = 0; i < peers.size(); ++i) {
                        sendAppendEntries(i);
                    }
                }
            } else {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_heartbeat).count() >= current_timeout) {
                    startElection(lock);
                }
            }
        }
    }

public:
    RaftNode(int id, std::vector<std::shared_ptr<Peer>> cluster_peers)
        : node_id(id), peers(cluster_peers), state(NodeState::FOLLOWER), running(true), current_leader_id(0) {
        srand(time(nullptr) + node_id + getpid()); //for randomness
        replay();
        last_heartbeat = std::chrono::steady_clock::now();
        current_timeout = getRandomTimeout();
        if (node_id == 1) current_timeout = 0; // for debugging
    }

    void startBackground() { 
        std::this_thread::sleep_for(std::chrono::seconds(3));
        last_heartbeat = std::chrono::steady_clock::now();
        background_thread = std::thread(&RaftNode::backgroundLoop, this);
    }

    ~RaftNode() {
        running = false;
        if (background_thread.joinable()) background_thread.join();
    }

    void submit(const Command& cmd) {
        std::unique_lock<std::mutex> lock(mtx);
        if (state != NodeState::LEADER) throw std::runtime_error("NOT_LEADER");
        uint64_t idx = store.logLen() + 1;
        LogEntry entry(store.getTerm(), idx, cmd);
        store.append(entry);
        for (size_t i = 0; i < peers.size(); ++i) {
            sendAppendEntries(i);
        }
        // Wait for this entry to be committed
        while (store.getCommit() < idx) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10)); //estimated time
            lock.lock();
        }
        // sfter commiting the entry , apply it to state machine
        applyCommitted();
    }

    std::string get(const std::string& key) {
        std::unique_lock<std::mutex> lock(mtx);
        if (state != NodeState::LEADER) throw std::runtime_error("NOT_LEADER");
        uint64_t idx = store.logLen() + 1;
        Command noop(OpType::NOOP);
        LogEntry entry(store.getTerm(), idx, noop);
        store.append(entry);
        for (size_t i = 0; i < peers.size(); ++i) {
            sendAppendEntries(i);
        }
        // Wait for NOOP to be committed
        while (store.getCommit() < idx) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            lock.lock();
        }
        // Verify still leader after commit
        if (state != NodeState::LEADER) {
            throw std::runtime_error("NOT_LEADER");
        }
        return sm.get(key);
    }

    std::string getStatus() const {
        std::unique_lock<std::mutex> lock(mtx);
        std::ostringstream oss;
        oss << "node_id: " << node_id << "\n";
        oss << "state: " << (state == NodeState::LEADER ? "LEADER" : (state == NodeState::CANDIDATE ? "CANDIDATE" : "FOLLOWER")) << "\n";
        oss << "term: " << store.getTerm() << "\n";
        oss << "log length: " << store.logLen() << "\n";
        oss << "commitIndex: " << store.getCommit() << "\n";
        return oss.str();
    }

    int getLeaderId() const { return current_leader_id; }

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
        bool logOk = (rpc.lastLogTerm > store.getLastLogTerm()) ||
                     (rpc.lastLogTerm == store.getLastLogTerm() && rpc.lastLogIndex >= store.getLastLogIndex());

        if ((votedFor.empty() || votedFor == std::to_string(rpc.candidateId)) && logOk) {
            store.setVoted(std::to_string(rpc.candidateId));
            resp.voteGranted = true;
            last_heartbeat = std::chrono::steady_clock::now();
            current_timeout = getRandomTimeout();
            std::cout << "[Node " << node_id << "] Voted for Node " << rpc.candidateId << " in term " << rpc.term << "\n";
        }
        return resp;
    }

    AppendEntriesResponse handleAppendEntries(const AppendEntries& rpc) {
        std::lock_guard<std::mutex> lock(mtx);
        AppendEntriesResponse resp;
        resp.term = store.getTerm();
        resp.success = false;

        if (rpc.term < store.getTerm()) return resp;

        if (rpc.term > store.getTerm()) {
            store.setTerm(rpc.term);
            state = NodeState::FOLLOWER;
            store.setVoted("");
        }
        // Always reset election timeout and update leader id
        last_heartbeat = std::chrono::steady_clock::now();
        current_timeout = getRandomTimeout();
        current_leader_id = rpc.leaderId;    //  updating leader id
        resp.term = rpc.term;

        // Log matching check
        if (rpc.prevLogIndex > 0) {
            if (rpc.prevLogIndex > store.getLastLogIndex()) return resp;
            if (store.getTermAt(rpc.prevLogIndex) != rpc.prevLogTerm) return resp;
        }

        // Conflict resolution
        uint64_t start = rpc.prevLogIndex + 1;
        for (size_t i = 0; i < rpc.entries.size(); ++i) {
            uint64_t idx = start + i;
            if (idx <= store.getLastLogIndex()) {
                if (store.getTermAt(idx) != rpc.entries[i].term) {
                    store.truncate(idx - 1);
                }
            }
        }
        // Append new entries
        for (const auto& entry : rpc.entries) {
            if (entry.index > store.getLastLogIndex()) {
                store.append(entry);
            }
        }

        // Update commit index
        if (rpc.leaderCommit > store.getCommit()) {
            uint64_t new_commit = std::min(rpc.leaderCommit, store.getLastLogIndex());
            store.setCommit(new_commit);
            
        }
        applyCommitted();
        resp.success = true;
        return resp;
    }
};

#endif