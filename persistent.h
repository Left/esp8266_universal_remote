#include <Arduino.h>
#include <FS.h>

namespace persistent {

    void stringToFile(const String& fileName, const String& value) {
        File f = SPIFFS.open(fileName.c_str(), "w");
        f.write((uint8_t*)value.c_str(), value.length());
        f.close();
    }

    String fileToString(const String& fileName) {
        if (SPIFFS.exists(fileName.c_str())) {
            File f = SPIFFS.open(fileName.c_str(), "r");
            std::vector<uint8_t> buf(f.size() + 1, 0);
            if (f && f.size()) {
            f.read((uint8_t*)(&buf[0]), buf.size());
            }
            f.close();
            return String((const char*)(&buf[0]));
        }
        return String();
    }

}