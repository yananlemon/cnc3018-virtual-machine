#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "core/lunyee_profile.h"

namespace cnc {
using Position = std::array<double, 3>;
enum class State { Alarm, Idle, Run, Jog, Hold, Home };
struct Config {
    Position travel=Lunyee3018ProUltra::travel;
    Position maxRate=Lunyee3018ProUltra::simulationRates; // Simulation defaults, not calibrated real-axis rates.
    Position acceleration=Lunyee3018ProUltra::simulationAcceleration;
    double pullOff = 2;
    double homeSeek = 500;
    double homeFeed = 100;
    bool softLimits = true;
};
struct Snapshot {
    Position position{}, planned{}, workOffset{};
    State state = State::Alarm;
    bool homed = false;
    double velocity = 0;
    std::size_t queueDepth = 0, rxBytes = 0;
    std::uint64_t received = 0, accepted = 0, executed = 0, rejected = 0;
    int homePhase = -1;
    double spindle = 0;
    std::array<bool,3> homeSwitches{}, referencedAxes{};
};
struct MotionStep {
    Position from{},to{};
    int kind=0; // 0 rapid, 1 feed, 2/3 arc, 4 jog, 5 home.
    double spindle=0;
};
// Single-thread ownership. UI/transport must marshal into the controller owner.
class Controller {
public:
    static constexpr std::size_t rxCapacity = 127, plannerCapacity = 15;
    explicit Controller(Config config = {});
    void connect(); // Emits boot banner on a new simulated session.
    void ingest(std::string_view bytes); // Realtime bytes never enter the RX queue.
    void tick(double seconds);
    void hardLimit(unsigned axis);
    Snapshot snapshot() const;
    std::vector<std::string> takeOutput();
    std::string status() const;
    static const char* stateName(State state);
    void setMotionObserver(std::function<void(const MotionStep&)> observer) { observer_=std::move(observer); }
private:
    struct Arc { Position start, center; double angle, sweep, radius, length, progress=0; };
    struct Block { Position target; double rate; bool jog; std::optional<Arc> arc = {}; int kind=0; };
    std::function<void(const MotionStep&)> observer_;
    Config config_;
    Position position_{150, 90, -40}, planned_{position_};
    std::array<bool,3> referencedAxes_{};
    Position workOffset_{};
    State state_ = State::Alarm;
    bool homed_ = false, absolute_ = true, inches_ = false;
    bool holding_ = false, cancelJog_ = false, overflow_ = false;
    bool comment_ = false, semicolon_ = false;
    int motion_ = 0, homePhase_ = -1;
    double feed_ = 0, velocity_ = 0;
    std::uint64_t received_ = 0, accepted_ = 0, executed_ = 0, rejected_ = 0;
    std::deque<char> rx_;
    std::string line_;
    std::optional<std::string> synchronized_;
    double dwell_ = -1, spindleRpm_ = 0;
    bool spindleOn_ = false;
    std::deque<Block> blocks_;
    std::optional<Block> homeBlock_;
    std::vector<std::string> output_;
    void emit(std::string text);
    void reset();
    void alarm(int code);
    void drain();
    void command(const std::string& text);
    void reply(int error = 0);
    void motion(double dt);
    void startHomePhase();
    bool within(const Position& p) const;
};
}
