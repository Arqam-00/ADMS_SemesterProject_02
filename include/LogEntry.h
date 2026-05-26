#ifndef LOGENTRY_H
#define LOGENTRY_H

#include "crc32.h"
#include "Command.h"
#include <vector>
#include <cstdint>
#include <arpa/inet.h>
#include <stdexcept>

struct LogEntry {
    uint64_t term;
    uint64_t index;//index in the log,actuall log
    std::vector<char> command;
    uint32_t crc;
    
    LogEntry() : term(0), index(0), crc(0) {}
    
    LogEntry(uint64_t t, uint64_t i, const Command& cmd) : term(t), index(i) {
        command = cmd.serialize();
        std::vector<char> data;
        for (int j = 0; j < 8; j++) {
            data.push_back((term >> (j * 8)) & 0xFF);
        }
        for (int j = 0; j < 8; j++) {
            data.push_back((index >> (j * 8)) & 0xFF);
        }
        data.insert(data.end(), command.begin(), command.end());
        //The correpted shall be slain(not actually slaid here(for now))
        crc = crc32_compute(data);
    }
    
    bool verify() const {
        std::vector<char> data;
        for (int j = 0; j < 8; j++) {
            data.push_back((term >> (j * 8)) & 0xFF);
        }
        for (int j = 0; j < 8; j++) {
            data.push_back((index >> (j * 8)) & 0xFF);
        }
        data.insert(data.end(), command.begin(), command.end());
        return crc == crc32_compute(data);
    }
    
    std::vector<char> serialize() const {
        std::vector<char> V;
        for (int j = 0; j < 8; j++) {
            V.push_back((term >> (j * 8)) & 0xFF);
        }
        for (int j = 0; j < 8; j++) {
            V.push_back((index >> (j * 8)) & 0xFF);
        }
        uint32_t cmd_len = htonl(static_cast<uint32_t>(command.size()));
        for (int j = 0; j < 4; j++) {
            V.push_back((cmd_len >> (j * 8)) & 0xFF);
        }
        // the actual command
        V.insert(V.end(), command.begin(), command.end());
        for (int j = 0; j < 4; j++) {
            V.push_back((crc >> (j * 8)) & 0xFF);
        }
        return V;
    }
    
    static LogEntry deserialize(const std::vector<char>& data, size_t& offset) {
        LogEntry temp;
        for (int j = 0; j < 8; j++) {
            temp.term |= (uint64_t)(uint8_t)data[offset + j] << (j * 8);
        }
        offset += 8;
        for (int j = 0; j < 8; j++) {
            temp.index |= (uint64_t)(uint8_t)data[offset + j] << (j * 8);
        }
        offset += 8;
        
        uint32_t cmd_len = 0;
        for (int j = 0; j < 4; j++) {
            cmd_len |= (uint32_t)(uint8_t)data[offset + j] << (j * 8);
        }
        cmd_len = ntohl(cmd_len);
        offset += 4;
        
        temp.command.assign(data.begin() + offset,data.begin() + offset + cmd_len);
        offset += cmd_len;
        for (int j = 0; j < 4; j++) {
            temp.crc |= (uint32_t)(uint8_t)data[offset + j] << (j * 8);
        }
        offset += 4;
        
        //Slay the corrupted!!!!!
        if (!temp.verify()) throw std::runtime_error("CRC mismatch in the log entry!");
        
        return temp;
    }
    
    Command getCommand() const { return Command::deserialize(command); }
};

#endif