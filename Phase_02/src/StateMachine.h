#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include "Command.h"
#include <unordered_map>
#include <string>

class StateMachine {
private:
    std::unordered_map<std::string, std::string> db;  // hash map, fast af
    
public:
    void apply(const Command& cmd) {
        if (cmd.getOp() == OpType::PUT) {
            db[cmd.getKey()] = cmd.getValue();  // insert or overwrite
        } else if (cmd.getOp() == OpType::DELETE) {
            db.erase(cmd.getKey());  // remove if exists, do nothing if not
        }
        // NOOP does nothing (for now atleast)
    }
    
    // Get a value by key, returns empty string if not found
    std::string get(const std::string& key) const {
        auto it = db.find(key);
        if (it != db.end()) {
            return it->second;
        }
        return "";
    }
};

#endif