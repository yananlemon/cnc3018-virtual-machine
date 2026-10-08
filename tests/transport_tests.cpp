#include "transport/win_serial.h"
#include <iostream>
int main() {
    cnc::WinSerial serial;
    for(auto name:{"","CON","C:","COM0","COM-1","COM1/path","COM999999999999999999999"}) {
        if(serial.open(name) || serial.isOpen() || serial.error().empty()) {
            std::cerr << "FAIL: invalid device name accepted\n"; return 1;
        }
        serial.close(); serial.close(); // Idempotent cleanup even before any handle exists.
    }
    serial.send("unused");
    if(!serial.poll().empty()) return 1;
    std::cout << "PASS: COM name validation, closed I/O and RAII lifecycle\n";
    return 0;
}
