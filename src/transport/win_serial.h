#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cnc {
// Opens an EXISTING COM endpoint. It does not create virtual serial ports.
class WinSerial {
public:
    WinSerial();
    ~WinSerial();
    WinSerial(const WinSerial&)=delete;
    WinSerial& operator=(const WinSerial&)=delete;
    static std::vector<std::string> ports();
    bool open(const std::string& port);
    void close();
    bool isOpen() const;
    void send(std::string_view bytes);
    std::string poll();
    std::string error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
