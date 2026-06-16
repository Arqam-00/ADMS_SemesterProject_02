#ifndef STORAGE_H
#define STORAGE_H

#include "LogEntry.h"
#include <fstream>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <iostream>

class Storage {
private:
    int log_fd;
    uint64_t term;
    uint64_t last_log_term;      // term of the last log entry (persistent)
    std::string voted;
    uint64_t log_length;         // also last_log_index

    // Volatile state (lost on crash)
    uint64_t commitIndex;
    uint64_t lastApplied;

    void save_meta() {
        int fd = open("raft.meta.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        for (int i = 0; i < 8; i++) {
            char b = (term >> (i * 8)) & 0xFF;
            write(fd, &b, 1);
        }
        uint32_t len = htonl(voted.size());
        for (int i = 0; i < 4; i++) {
            char b = (len >> (i * 8)) & 0xFF;
            write(fd, &b, 1);
        }
        write(fd, voted.data(), voted.size());
        fsync(fd);
        close(fd);
        rename("raft.meta.tmp", "raft.meta");
    }

    void load_meta() {
        std::ifstream f("raft.meta", std::ios::binary);
        if (!f) {
            term = 0;
            last_log_term = 0;
            voted = "";
            return;
        }
        term = 0;
        for (int i = 0; i < 8; i++) {
            term |= (uint64_t)(uint8_t)f.get() << (i * 8);
        }
        last_log_term = 0;
        uint32_t len = 0;
        for (int i = 0; i < 4; i++) {
            len |= (uint32_t)(uint8_t)f.get() << (i * 8);
        }
        len = ntohl(len);
        if (len > 0) {
            std::vector<char> buf(len);
            f.read(buf.data(), len);
            voted = std::string(buf.begin(), buf.end());
        } else {
            voted = "";
        }
    }

public:
    Storage() : log_fd(-1), term(0), last_log_term(0), log_length(0), commitIndex(0), lastApplied(0) {
        log_fd = open("raft.log", O_RDWR | O_CREAT | O_APPEND, 0666);
        load_meta();
        // Rebuild log length and last_log_term from disk
        auto entries = readAll();
        if (!entries.empty()) {
            log_length = entries.back().index;
            last_log_term = entries.back().term;
            save_meta(); // ensure consistency
        }
    }

    ~Storage() { if (log_fd >= 0) close(log_fd); }

    // Basic getters/setters
    uint64_t logLen() const { return log_length; }
    uint64_t getTerm() const { return term; }
    void setTerm(uint64_t t) {
        term = t;
        save_meta();
    }

    uint64_t getLastLogIndex() const { return log_length; }
    uint64_t getLastLogTerm() const { return last_log_term; }

    uint64_t getCommit() const { return commitIndex; }
    void setCommit(uint64_t c) { commitIndex = c; }
    uint64_t getApplied() const { return lastApplied; }
    void setApplied(uint64_t a) { lastApplied = a; }

    std::string getVoted() const { return voted; }
    void setVoted(const std::string& v) {
        voted = v;
        save_meta();
    }

    void append(const LogEntry& entry) {
        auto data = entry.serialize();
        write(log_fd, data.data(), data.size());
        fsync(log_fd);
        log_length = entry.index;
        last_log_term = entry.term;
        save_meta();
    }

    // Read all entries from log file (const, does not modify state)
    std::vector<LogEntry> readAll() const {
        std::vector<LogEntry> entries;
        std::ifstream f("raft.log", std::ios::binary);
        if (!f) return entries;
        std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
        size_t offset = 0;
        while (offset < data.size()) {
            try {
                LogEntry entry = LogEntry::deserialize(data, offset);
                if (entry.verify()) {
                    entries.push_back(entry);
                } else {
                    std::cerr << "Log corruption detected. Stopping read." << std::endl;
                    break;
                }
            } catch (...) {
                std::cerr << "Failed to deserialize log entry. Stopping read." << std::endl;
                break;
            }
        }
        return entries;
    }

    // Phase 3 helper methods 
    uint64_t getTermAt(uint64_t index) {
        if (index == 0 || index > log_length) return 0;
        auto entries = readAll();
        if (index <= entries.size())
            return entries[index-1].term;
        return 0;
    }

    void truncate(uint64_t keep_up_to_index) {
        if (keep_up_to_index >= log_length) return;
        auto entries = readAll();
        if (keep_up_to_index >= entries.size()) return;
        entries.resize(keep_up_to_index);
        // Rewrite the log file
        if (log_fd >= 0) close(log_fd);
        log_fd = open("raft.log", O_RDWR | O_CREAT | O_TRUNC, 0666);
        for (auto& e : entries) {
            auto data = e.serialize();
            write(log_fd, data.data(), data.size());
        }
        fsync(log_fd);
        log_length = keep_up_to_index;
        last_log_term = (keep_up_to_index == 0) ? 0 : entries.back().term;
        save_meta();
    }

    std::vector<LogEntry> readFrom(uint64_t start_index) {
        if (start_index < 1 || start_index > log_length) return {};
        auto entries = readAll();
        if (start_index > entries.size()) return {};
        return std::vector<LogEntry>(entries.begin() + (start_index - 1), entries.end());
    }

    std::vector<LogEntry> readRange(uint64_t start, uint64_t end) {
        if (start < 1 || end < start || end > log_length) return {};
        auto entries = readAll();
        if (end > entries.size()) return {};
        return std::vector<LogEntry>(entries.begin() + (start - 1), entries.begin() + end);
    }
};

#endif