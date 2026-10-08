#include "transport/win_serial.h"
#include <array>
#include <deque>
#include <cctype>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace cnc {
struct WinSerial::Impl {
    std::string error;
#ifdef _WIN32
    HANDLE handle=INVALID_HANDLE_VALUE;
    OVERLAPPED read{},write{};
    std::array<char,4096> buffer{};
    bool readPending=false,writePending=false;
    std::deque<std::string> tx;
    std::size_t txBytes=0,offset=0;
    void fail(const char* action) { error=std::string(action)+" (Windows error "+std::to_string(GetLastError())+")"; }
#endif
};
WinSerial::WinSerial():impl_(std::make_unique<Impl>()) {}
WinSerial::~WinSerial() { close(); }
std::vector<std::string> WinSerial::ports() {
    std::vector<std::string> result;
#ifdef _WIN32
    std::array<char,32768> path{};
    for(int i=1;i<=256;++i) {
        auto port="COM"+std::to_string(i);
        if(QueryDosDeviceA(port.c_str(),path.data(),static_cast<DWORD>(path.size()))) result.push_back(port);
    }
#endif
    return result;
}
bool WinSerial::isOpen() const {
#ifdef _WIN32
    return impl_->handle!=INVALID_HANDLE_VALUE;
#else
    return false;
#endif
}
bool WinSerial::open(const std::string& port) {
    close(); impl_->error.clear();
    unsigned number=0;
    bool valid=port.size()>3 && std::toupper(static_cast<unsigned char>(port[0]))=='C' &&
        std::toupper(static_cast<unsigned char>(port[1]))=='O' && std::toupper(static_cast<unsigned char>(port[2]))=='M';
    if(valid) for(std::size_t i=3;i<port.size();++i) {
        if(port[i]<'0' || port[i]>'9') { valid=false; break; }
        number=number*10+static_cast<unsigned>(port[i]-'0');
        if(number>65535) { valid=false; break; }
    }
    if(!valid || number==0) { impl_->error="Invalid COM name (expected COM1..COM65535)"; return false; }
#ifdef _WIN32
    impl_->handle=CreateFileA(("\\\\.\\"+port).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
    if(!isOpen()) { impl_->fail("Open COM"); return false; }
    DCB dcb{}; dcb.DCBlength=sizeof(dcb);
    if(!GetCommState(impl_->handle,&dcb)) { impl_->fail("Read port configuration"); close(); return false; }
    dcb.BaudRate=CBR_115200; dcb.ByteSize=8; dcb.Parity=NOPARITY; dcb.StopBits=ONESTOPBIT;
    dcb.fBinary=TRUE; dcb.fParity=FALSE; dcb.fOutxCtsFlow=FALSE; dcb.fOutxDsrFlow=FALSE;
    dcb.fDtrControl=DTR_CONTROL_DISABLE; dcb.fDsrSensitivity=FALSE;
    dcb.fOutX=FALSE; dcb.fInX=FALSE; dcb.fRtsControl=RTS_CONTROL_DISABLE;
    dcb.fAbortOnError=FALSE; dcb.fNull=FALSE; dcb.fErrorChar=FALSE;
    COMMTIMEOUTS timeouts{}; timeouts.ReadIntervalTimeout=MAXDWORD;
    // Immediate reads with overlapped writes; never waits for hardware in poll().
    if(!SetCommState(impl_->handle,&dcb) || !SetCommTimeouts(impl_->handle,&timeouts) || !SetupComm(impl_->handle,4096,4096)) {
        impl_->fail("Configure 115200 8N1"); close(); return false;
    }
    impl_->read.hEvent=CreateEventA(nullptr,TRUE,FALSE,nullptr);
    impl_->write.hEvent=CreateEventA(nullptr,TRUE,FALSE,nullptr);
    if(!impl_->read.hEvent || !impl_->write.hEvent) { impl_->fail("Create COM event"); close(); return false; }
    PurgeComm(impl_->handle,PURGE_RXCLEAR|PURGE_TXCLEAR);
    return true;
#else
    (void)port; impl_->error="Windows COM transport is only available on Windows"; return false;
#endif
}
void WinSerial::close() {
#ifdef _WIN32
    if(isOpen()) {
        CancelIoEx(impl_->handle,nullptr);
        // Cancellation must finish before buffers/OVERLAPPED storage are reused.
        DWORD ignored=0;
        if(impl_->readPending) GetOverlappedResult(impl_->handle,&impl_->read,&ignored,TRUE);
        if(impl_->writePending) GetOverlappedResult(impl_->handle,&impl_->write,&ignored,TRUE);
        CloseHandle(impl_->handle); impl_->handle=INVALID_HANDLE_VALUE;
    }
    if(impl_->read.hEvent) CloseHandle(impl_->read.hEvent);
    if(impl_->write.hEvent) CloseHandle(impl_->write.hEvent);
    impl_->read={}; impl_->write={}; impl_->readPending=impl_->writePending=false;
    impl_->tx.clear(); impl_->txBytes=impl_->offset=0;
#endif
}
void WinSerial::send(std::string_view bytes) {
#ifdef _WIN32
    if(!isOpen() || bytes.empty()) return;
    if(impl_->txBytes+bytes.size()>65536) { impl_->error="COM TX queue overflow; session closed"; close(); return; }
    impl_->tx.emplace_back(bytes); impl_->txBytes+=bytes.size();
#else
    (void)bytes;
#endif
}
std::string WinSerial::poll() {
    std::string received;
#ifdef _WIN32
    if(!isOpen()) return received;
    DWORD count=0;
    if(impl_->writePending) {
        if(GetOverlappedResult(impl_->handle,&impl_->write,&count,FALSE)) {
            impl_->writePending=false; impl_->offset+=count; impl_->txBytes-=count;
        } else if(GetLastError()!=ERROR_IO_INCOMPLETE) { impl_->fail("COM write"); close(); return received; }
    }
    if(!impl_->writePending && !impl_->tx.empty()) {
        if(impl_->offset==impl_->tx.front().size()) { impl_->tx.pop_front(); impl_->offset=0; }
        if(!impl_->tx.empty()) {
            auto& message=impl_->tx.front(); ResetEvent(impl_->write.hEvent);
            if(WriteFile(impl_->handle,message.data()+impl_->offset,static_cast<DWORD>(message.size()-impl_->offset),&count,&impl_->write)) {
                impl_->offset+=count; impl_->txBytes-=count;
            } else if(GetLastError()==ERROR_IO_PENDING) impl_->writePending=true;
            else { impl_->fail("COM write"); close(); return received; }
        }
    }
    if(impl_->readPending) {
        if(GetOverlappedResult(impl_->handle,&impl_->read,&count,FALSE)) {
            impl_->readPending=false; received.append(impl_->buffer.data(),count);
        } else if(GetLastError()!=ERROR_IO_INCOMPLETE) { impl_->fail("COM read"); close(); return received; }
    }
    if(!impl_->readPending) {
        ResetEvent(impl_->read.hEvent);
        if(ReadFile(impl_->handle,impl_->buffer.data(),static_cast<DWORD>(impl_->buffer.size()),&count,&impl_->read)) received.append(impl_->buffer.data(),count);
        else if(GetLastError()==ERROR_IO_PENDING) impl_->readPending=true;
        else { impl_->fail("COM read"); close(); }
    }
#endif
    return received;
}
std::string WinSerial::error() const { return impl_->error; }
}
