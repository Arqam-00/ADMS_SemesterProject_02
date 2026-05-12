#ifndef COMMAND_H
#define COMMAND_H

#include <vector>
#include <string>
#include <cstdint>
#include <arpa/inet.h>
#include <stdexcept>

enum class OpType : uint8_t {
    PUT = 1,
    DELETE = 2,
    NOOP = 3
};

class Command {
private:
    OpType oper;
    std::string Key;
    std::string Val;
public:
    Command() : oper(OpType::NOOP) {}
    
    Command(OpType op, const std::string& key = "", const std::string& value = ""): oper(op), Key(key), Val(value) {}
    
    std::vector<char> serialize() const {
        std::vector<char> V;
        V.push_back(static_cast<char>(oper));
        // Bytes 1 & 2, key length
        uint16_t Keylen = htons(static_cast<uint16_t>(Key.size()));
        V.push_back((Keylen >> 8) & 0xFF);
        V.push_back(Keylen & 0xFF);
        // Bytes 3-4, value length
        uint16_t val_len = htons(static_cast<uint16_t>(Val.size()));
        V.push_back((val_len >> 8) & 0xFF);
        V.push_back(val_len & 0xFF);
        
        //key
        V.insert(V.end(), Key.begin(), Key.end());
        
        //val
        V.insert(V.end(), Val.begin(), Val.end());
        return V;
    }
    
    // Reconstruct command from bytes read from log
    static Command deserialize(const std::vector<char>& data) {
        if (data.size() < 5) throw std::runtime_error("Command too short");
        size_t pos = 0;
        OpType op = static_cast<OpType>(data[pos++]);
        
        // 2 bytes for lenght of key
        uint16_t Keylen = (static_cast<uint8_t>(data[pos]) << 8) | static_cast<uint8_t>(data[pos + 1]);
        Keylen = ntohs(Keylen);
        pos += 2;
        
        // 3,4 for val length
        uint16_t val_len = (static_cast<uint8_t>(data[pos]) << 8) | static_cast<uint8_t>(data[pos + 1]);
        val_len = ntohs(val_len);
        pos += 2;
        
        std::string key(data.begin() + pos, data.begin() + pos + Keylen);
        pos += Keylen;
        std::string value(data.begin() + pos, data.begin() + pos + val_len);
        return Command(op, key, value);
    }
    
    OpType getOp() const { return oper; }
    const std::string& getKey() const { return Key; }
    const std::string& getValue() const { return Val; }
    
    bool isNoop() const { return oper == OpType::NOOP; }
    bool isPut() const { return oper == OpType::PUT; }
    bool isDelete() const { return oper == OpType::DELETE; }
    
    //for debbuging purposes
    std::string toString() const {
        if (oper == OpType::PUT) {
            return "PUT " + Key + "=" + Val;
        } else if (oper == OpType::DELETE) {
            return "DELETE " + Key;
        } else {
            return "NOOP";
        }
    }
    
};

#endif