#ifndef STORAGE_H
#define STORAGE_H

#include "LogEntry.h"
#include <fstream>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>

//Handles all za disk I/O with fsync
class Storage {
private:
    int log_fd;
    uint64_t term;
    uint64_t commit;    //highest committed index
    uint64_t applied;      // highest applied to state machine
    std::string voted;
    uint64_t log_length;

    void save_meta() {
        std::ofstream f("raft.meta.tmp", std::ios::binary);
        for (int i = 0; i < 8; i++) f.put((term >> (i * 8)) & 0xFF);        //term 
        uint32_t len = htonl(voted.size());
        for (int i = 0; i < 4; i++) f.put((len >> (i * 8)) & 0xFF);
        f.write(voted.data(), voted.size());
        
        for (int i = 0; i < 8; i++) f.put((commit >> (i * 8)) & 0xFF);
        for (int i = 0; i < 8; i++) f.put((applied >> (i * 8)) & 0xFF);
        
        // f.flush();
        // fsync(fileno(f));  //force it doown to the disk
        f.close();
        int fd = open("raft.meta.tmp", O_RDONLY);
        if(fd >= 0){
            fsync(fd);//NOW THIS WORKS
            close(fd);
        }
        rename("raft.meta.tmp", "raft.meta");  //atomic swap
    }
    
    void load_meta() {
        std::ifstream f("raft.meta", std::ios::binary);
        if (!f) {
            //what if there was no metadata(either dataloss or start?)
            term = 0;
            voted = "";
            commit = 0;
            applied = 0;
            save_meta();
            return;
        }
        std::cout << "============= Metadata found ==================";
        term = 0;
        for (int i = 0; i < 8; i++) term |= (uint64_t)(uint8_t)f.get() << (i * 8);
        uint32_t len = 0;
        for (int i = 0; i < 4; i++) len |= (uint32_t)(uint8_t)f.get() << (i * 8);
        len = ntohl(len);
        voted.resize(len);
        f.read(&voted[0], len);
        commit = 0;
        for (int i = 0; i < 8; i++) commit |= (uint64_t)(uint8_t)f.get() << (i * 8);
        applied = 0;
        for (int i = 0; i < 8; i++) applied |= (uint64_t)(uint8_t)f.get() << (i * 8);
        std::cout << "Loaded: term=" << term << ", commit=" << commit << ", applied=" << applied << std::endl;
    }
    
public:
    Storage() {
        load_meta();
        log_fd = open("raft.log", O_RDWR | O_CREAT | O_APPEND, 0644);
        if (log_fd < 0) {
            throw std::runtime_error("Cannot open raft.log");
        }
        auto entries = readAll();
        log_length = entries.size();
        std::cout << "Storage initialized. Found " << log_length << " existing log entries\n";
    }
    
    ~Storage() {
        if (log_fd >= 0) {
            fsync(log_fd);
            close(log_fd);
        }
    }
    
    uint64_t getTerm() const { return term; }
    void setTerm(uint64_t t) { 
        term = t;
        save_meta();
     }
    uint64_t getCommit() const { return commit; }
    void setCommit(uint64_t c) { 
        commit = c;
        save_meta(); 
        }
    uint64_t getApplied() const { return applied; }
    void setApplied(uint64_t a) { 
        applied = a;
        save_meta();
    }
    std::string getVoted() const { return voted; }
    void setVoted(const std::string& v) { 
        voted = v; 
        save_meta();
    }
    
    //Append a log entry to disk
    void append(const LogEntry& entry) {
        auto data = entry.serialize();
        write(log_fd, data.data(), data.size());
        fsync(log_fd);  // pray to god
        log_length++;
    }
    
    // Read all log entries from disk (for recovery purposes)
    std::vector<LogEntry> readAll() {
        std::vector<LogEntry> entries;
        std::ifstream f("raft.log", std::ios::binary);
        if (!f) return entries;
        std::vector<char> data((std::istreambuf_iterator<char>(f)), {});
        size_t offset = 0;
        while (offset < data.size()) {
            try {
                entries.push_back(LogEntry::deserialize(data, offset));
            } catch (...) {
                break;  // corrupted entry, stop reading or you will be tainted by the abyss
            }
        }
        return entries;
    }
    
    uint64_t logLen() {
        return log_length;
    }
};

#endif