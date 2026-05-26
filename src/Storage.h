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
    std::string voted;
    uint64_t log_length;

    // Volatile State - Lost on crash!
    uint64_t commitIndex;
    uint64_t lastApplied;

    void save_meta() {
        std::ofstream f("raft.meta.tmp", std::ios::binary);
        for (int i = 0; i < 8; i++) f.put((term >> (i * 8)) & 0xFF);
        uint32_t len = htonl(voted.size());
        for (int i = 0; i < 4; i++) f.put((len >> (i * 8)) & 0xFF);
        f.write(voted.data(), voted.size());
        f.close();
        rename("raft.meta.tmp", "raft.meta");
    }

    void load_meta() {
        std::ifstream f("raft.meta", std::ios::binary);
        if (!f) {
            term = 0;
            voted = "";
            return;
        }
        term = 0;
        for (int i = 0; i < 8; i++) {
            term |= (uint64_t)(uint8_t)f.get() << (i * 8);
        }
        uint32_t len = 0;
        for (int i = 0; i < 4; i++) {
            len |= (uint32_t)(uint8_t)f.get() << (i * 8);
        }
        len = ntohl(len);
        if (len > 0) {
            std::vector<char> buf(len);
            f.read(buf.data(), len);
            voted = std::string(buf.begin(), buf.end());
        }
    }

public:
    Storage() : log_length(0), commitIndex(0), lastApplied(0) {
        log_fd = open("raft.log", O_RDWR | O_CREAT | O_APPEND, 0666);
        load_meta();
    }

    ~Storage() { close(log_fd); }

    uint64_t logLen() const { return log_length; }
    uint64_t getTerm() const { return term; }
    void setTerm(uint64_t t) {
        term = t;
        save_meta();
    }

    // Volatile Getters/Setters (No saving to disk!)
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
        log_length++;
    }

    std::vector<LogEntry> readAll() {
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
                }
                else {
                    std::cerr << "Log corruption detected at index " << entry.index << std::endl;
                    break;
                }
            }
            catch (...) {
                break;
            }
        }
        log_length = entries.size();
        return entries;
    }
};
#endif