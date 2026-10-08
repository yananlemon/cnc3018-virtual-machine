#pragma once
#include "core/controller.h"
#include <deque>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace cnc {
// Host-side byte counting. Exactly one sender owns a session's line-response ledger.
class Streamer {
public:
    void start(std::vector<std::string> lines) {
        if(!pending_.empty()) throw std::logic_error("Unacknowledged lines remain");
        for(const auto& line:lines) if(line.size()+1>Controller::rxCapacity || line.size()>79)
            throw std::invalid_argument("Program line exceeds simulator line/RX limit");
        clear(); lines_=std::move(lines); sending_=true;
    }
    std::optional<std::string> next() {
        if(!sending_ || sent_>=lines_.size()) return {};
        const auto size=lines_[sent_].size()+1;
        if(inflight_+size>Controller::rxCapacity) return {};
        pending_.push_back(size); inflight_+=size;
        return lines_[sent_++]+"\n";
    }
    void acknowledge(bool success) {
        if(pending_.empty()) return;
        inflight_-=pending_.front(); pending_.pop_front(); ++confirmed_;
        if(!success) { ++errors_; sending_=false; }
    }
    void stopSending() { sending_=false; } // Existing RX/planner motion still executes.
    void clear() { lines_.clear(); pending_.clear(); inflight_=sent_=confirmed_=errors_=0; sending_=false; }
    bool active() const { return (sending_ && sent_<lines_.size()) || !pending_.empty(); }
    bool sending() const { return sending_ && sent_<lines_.size(); }
    std::size_t sent() const { return sent_; }
    std::size_t confirmed() const { return confirmed_; }
    std::size_t total() const { return lines_.size(); }
    std::size_t inflight() const { return inflight_; }
    std::size_t errors() const { return errors_; }
private:
    std::vector<std::string> lines_;
    std::deque<std::size_t> pending_;
    std::size_t inflight_=0,sent_=0,confirmed_=0,errors_=0;
    bool sending_=false;
};
}
