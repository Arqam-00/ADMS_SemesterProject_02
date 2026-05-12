#ifndef RPC_H
#define RPC_H

#include "LogEntry.h"
#include <vector>
#include <cstdint>
#include <stdexcept>

inline void push_uint64(std::vector<char>& v, uint64_t val) {
    for (int i = 0; i < 8; i++) v.push_back((val >> (i * 8)) & 0xFF);
}

inline uint64_t read_uint64(const std::vector<char>& data, size_t& offset) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) val |= (uint64_t)(uint8_t)data[offset++] << (i * 8);
    return val;
}

// --- Heartbeat & Replication RPC ---
struct AppendEntries {
    uint64_t term;
    uint64_t leaderId;
    uint64_t prevLogIndex;
    uint64_t prevLogTerm;
    uint64_t leaderCommit;
    std::vector<LogEntry> entries; 

    std::vector<char> serialize() const {
        std::vector<char> v;
        v.push_back('A'); 
        push_uint64(v, term);
        push_uint64(v, leaderId);
        push_uint64(v, prevLogIndex);
        push_uint64(v, prevLogTerm);
        push_uint64(v, leaderCommit);
        
        uint32_t count = entries.size();
        for (int i = 0; i < 4; i++) v.push_back((count >> (i * 8)) & 0xFF);
        for (const auto& entry : entries) {
            auto bin_entry = entry.serialize();
            uint32_t len = bin_entry.size();
            for (int i = 0; i < 4; i++) v.push_back((len >> (i * 8)) & 0xFF);
            v.insert(v.end(), bin_entry.begin(), bin_entry.end());
        }
        return v;
    }

    static AppendEntries deserialize(const std::vector<char>& data) {
        AppendEntries rpc;
        size_t offset = 1; 
        rpc.term = read_uint64(data, offset);
        rpc.leaderId = read_uint64(data, offset);
        rpc.prevLogIndex = read_uint64(data, offset);
        rpc.prevLogTerm = read_uint64(data, offset);
        rpc.leaderCommit = read_uint64(data, offset);
        
        uint32_t count = 0;
        for (int i = 0; i < 4; i++) count |= (uint32_t)(uint8_t)data[offset++] << (i * 8);
        for (uint32_t i = 0; i < count; i++) {
            uint32_t len = 0;
            for (int j = 0; j < 4; j++) len |= (uint32_t)(uint8_t)data[offset++] << (j * 8);
            rpc.entries.push_back(LogEntry::deserialize(data, offset)); 
        }
        return rpc;
    }
};

struct AppendEntriesResponse {
    uint64_t term;
    bool success;

    std::vector<char> serialize() const {
        std::vector<char> v;
        v.push_back('R'); 
        push_uint64(v, term);
        v.push_back(success ? 1 : 0);
        return v;
    }

    static AppendEntriesResponse deserialize(const std::vector<char>& data) {
        AppendEntriesResponse resp;
        size_t offset = 1; 
        resp.term = read_uint64(data, offset);
        resp.success = data[offset] != 0;
        return resp;
    }
};

// --- Election RPC ---
struct RequestVote {
    uint64_t term;
    uint64_t candidateId;
    uint64_t lastLogIndex;
    uint64_t lastLogTerm;

    std::vector<char> serialize() const {
        std::vector<char> v;
        v.push_back('V'); 
        push_uint64(v, term);
        push_uint64(v, candidateId);
        push_uint64(v, lastLogIndex);
        push_uint64(v, lastLogTerm);
        return v;
    }

    static RequestVote deserialize(const std::vector<char>& data) {
        RequestVote rpc;
        size_t offset = 1; 
        rpc.term = read_uint64(data, offset);
        rpc.candidateId = read_uint64(data, offset);
        rpc.lastLogIndex = read_uint64(data, offset);
        rpc.lastLogTerm = read_uint64(data, offset);
        return rpc;
    }
};

struct RequestVoteResponse {
    uint64_t term;
    bool voteGranted;

    std::vector<char> serialize() const {
        std::vector<char> v;
        v.push_back('W'); 
        push_uint64(v, term);
        v.push_back(voteGranted ? 1 : 0);
        return v;
    }

    static RequestVoteResponse deserialize(const std::vector<char>& data) {
        RequestVoteResponse resp;
        size_t offset = 1; 
        resp.term = read_uint64(data, offset);
        resp.voteGranted = data[offset] != 0;
        return resp;
    }
};

#endif