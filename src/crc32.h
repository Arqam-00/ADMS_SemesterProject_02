#ifndef CRC32_H
#define CRC32_H // to be used for logging

#include <cstdint> // gives custon size of integers
#include <cstddef>
#include <vector>
#include <string>
// cyclic redundancy check,i did not invent this
static uint32_t crc32_compute(const void* data, size_t len) {
    static uint32_t table[256];
    static bool inited = false;
    if (!inited) {
        uint32_t poly = 0xEDB88320;
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++) {
                if (crc & 1) crc = (crc >> 1) ^ poly;
                else crc >>= 1;
            }
            table[i] = crc;
        }
        inited = true;
    }
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t i = 0; i < len; i++) {
        crc = (crc >> 8) ^ table[(crc ^ bytes[i]) & 0xFF];
    }
    return crc ^ 0xFFFFFFFF;
}
//Facade pattern to make our lives less miserable
uint32_t crc32_compute(const std::vector<char>& data) {
    return crc32_compute(data.data(), data.size());
}

uint32_t crc32_compute(const std::string& data) {
    return crc32_compute(data.data(), data.size());
}

#endif