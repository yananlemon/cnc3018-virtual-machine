#include "core/controller.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace cnc {
namespace {
double distance(const Position& a, const Position& b) {
    double sum = 0;
    for (int i=0; i<3; ++i) sum += (a[i]-b[i])*(a[i]-b[i]);
    return std::sqrt(sum);
}
std::string coordinates(const Position& p) {
    std::ostringstream s; s.imbue(std::locale::classic());
    s << std::fixed << std::setprecision(3) << p[0] << ',' << p[1] << ',' << p[2];
    return s.str();
}
}
Controller::Controller(Config config) : config_(config) {
    for (int i=0;i<3;++i) {
        if (!(std::isfinite(config_.travel[i]) && std::isfinite(config_.maxRate[i]) && std::isfinite(config_.acceleration[i]) &&
              config_.travel[i]>config_.pullOff && config_.maxRate[i]>0 && config_.acceleration[i]>0))
            throw std::invalid_argument("Invalid machine profile");
        position_[i] = (i<2?1:-1)*config_.travel[i]/2;
    }
    planned_ = position_;
    if(!std::isfinite(config_.pullOff) || config_.pullOff<=0 || !std::isfinite(config_.homeSeek) ||
       !std::isfinite(config_.homeFeed) || config_.homeSeek<=0 || config_.homeFeed<=0)
        throw std::invalid_argument("Invalid homing profile");
}
const char* Controller::stateName(State s) {
    switch(s) {
    case State::Alarm: return "Alarm"; case State::Idle: return "Idle";
    case State::Run: return "Run"; case State::Jog: return "Jog";
    case State::Hold: return "Hold"; case State::Home: return "Home";
    }
    return "Alarm";
}
void Controller::emit(std::string text) { output_.push_back(std::move(text)+"\r\n"); }
void Controller::connect() {
    emit("Grbl 1.1h ['$' for help]");
    emit("[MSG:LUNYEE 3018 Pro Ultra VM 0.3 - top-left XY0 user configuration]");
    if (state_==State::Alarm) emit("[MSG:'$H'|'$X' to unlock]");
}
void Controller::reset() {
    const bool interrupted = state_==State::Run || state_==State::Jog || state_==State::Home || state_==State::Hold;
    blocks_.clear(); homeBlock_.reset(); homePhase_=-1; rx_.clear(); line_.clear();
    holding_=cancelJog_=overflow_=comment_=semicolon_=false;
    absolute_=true; inches_=false; motion_=0; feed_=velocity_=0; planned_=position_;
    synchronized_.reset(); dwell_=-1; spindleOn_=false; spindleRpm_=0; workOffset_={};
    homed_=false; referencedAxes_={}; state_=State::Alarm;
    connect(); if(interrupted) emit("ALARM:3");
}
void Controller::alarm(int code) {
    blocks_.clear(); homeBlock_.reset(); homePhase_=-1;
    velocity_=0; planned_=position_; homed_=false; referencedAxes_={}; holding_=cancelJog_=false;
    state_=State::Alarm; emit("ALARM:"+std::to_string(code));
    synchronized_.reset(); dwell_=-1; spindleOn_=false;
}
void Controller::hardLimit(unsigned axis) { if(axis<3) alarm(1); }
void Controller::ingest(std::string_view bytes) {
    for (unsigned char c : bytes) {
        if(c==0x18) { reset(); continue; }
        if(c=='?') { if(state_!=State::Home) emit(status()); continue; }
        if(c=='!') {
            if(state_==State::Jog) { holding_=true; cancelJog_=true; }
            else if(state_==State::Run || state_==State::Hold) { holding_=true; if(dwell_>=0) state_=State::Hold; }
            continue;
        }
        if(c=='~') {
            if(state_==State::Hold && !cancelJog_) { holding_=false; state_=State::Run; }
            continue;
        }
        if(c==0x85) { if(state_==State::Jog) { holding_=true; cancelJog_=true; } continue; }
        // Extended realtime overrides are deliberately unsupported in this first profile.
        if(c>=0x80) continue;
        if(rx_.size()>=rxCapacity) {
            // Unlike an AVR silent overrun, fail closed for a visible simulator diagnostic.
            rx_.clear(); line_.clear(); overflow_=comment_=semicolon_=false;
            alarm(1); emit("[MSG:Simulator RX overflow; reset required]");
            continue;
        }
        rx_.push_back(static_cast<char>(c));
    }
    drain();
}
void Controller::reply(int error) {
    if(error) { ++rejected_; emit("error:"+std::to_string(error)); }
    else { ++accepted_; emit("ok"); }
}
bool Controller::within(const Position& p) const {
    for(int i=0;i<3;++i) {
        const double lower=i<2?0:-config_.travel[i],upper=i<2?config_.travel[i]:0;
        if(p[i]<lower-1e-8 || p[i]>upper+1e-8) return false;
    }
    return true;
}
void Controller::drain() {
    if(state_==State::Home) return; // $H replies only after completion.
    if(dwell_>=0) return;
    if(synchronized_) {
        if(!blocks_.empty()) return;
        auto text=std::move(*synchronized_); synchronized_.reset(); command(text);
        if(dwell_>=0) return;
    }
    while(!rx_.empty() && blocks_.size()<plannerCapacity) {
        const char c = rx_.front(); rx_.pop_front();
        if(c=='\r' || c=='\n') {
            ++received_;
            if(overflow_) reply(11); else command(line_);
            line_.clear(); overflow_=comment_=semicolon_=false;
            if(state_==State::Home || synchronized_ || dwell_>=0) return;
        } else if(c==';') semicolon_=true;
        else if(c=='(') comment_=true;
        else if(c==')') comment_=false;
        else if(!comment_ && !semicolon_ && !std::isspace(static_cast<unsigned char>(c))) {
            if(line_.size()>=79) overflow_=true;
            else line_+=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
}
void Controller::command(const std::string& text) {
    if(text.empty()) { reply(); return; }
    if(text=="$") { emit("[HLP:$$ $# $G $I $X $H $J= ~ ! ? ctrl-x]"); reply(); return; }
    if(text=="$I") { emit("[VER:1.1h:CNC3018-VM-0.3]"); emit("[OPT:,15,127]"); reply(); return; }
    if(text=="$$") {
        emit("$20="+std::to_string(config_.softLimits?1:0)); emit("$21=1"); emit("$22=1");
        emit("$23=3"); emit("$24="+std::to_string(config_.homeFeed));
        emit("$25="+std::to_string(config_.homeSeek)); emit("$27="+std::to_string(config_.pullOff));
        for(int i=0;i<3;++i) {
            emit("$"+std::to_string(110+i)+"="+std::to_string(config_.maxRate[i]));
            emit("$"+std::to_string(120+i)+"="+std::to_string(config_.acceleration[i]));
            emit("$"+std::to_string(130+i)+"="+std::to_string(config_.travel[i]));
        }
        reply(); return;
    }
    if(text=="$G") {
        emit("[GC:G"+std::to_string(motion_)+" G54 G17 G"+std::string(inches_?"20":"21")+
             " G"+std::string(absolute_?"90":"91")+" G94 M"+std::string(spindleOn_?"3":"5")+
             " M9 F"+std::to_string(feed_/(inches_?25.4:1.0))+" S"+std::to_string(spindleRpm_)+"]");
        reply(); return;
    }
    if(text=="$#") {
        for(int i=54;i<=59;++i) emit("[G"+std::to_string(i)+":0.000,0.000,0.000]");
        emit("[G92:"+coordinates(workOffset_)+"]"); reply(); return;
    }
    if(text=="$X") {
        if(state_!=State::Alarm && state_!=State::Idle) { reply(8); return; }
        state_=State::Idle; emit("[MSG:Caution: Unlocked]"); reply(); return;
    }
    if(text=="$H") {
        if(state_!=State::Alarm && state_!=State::Idle) { reply(8); return; }
        blocks_.clear(); holding_=cancelJog_=false; velocity_=0;
        state_=State::Home; homed_=false; referencedAxes_={}; homePhase_=0; startHomePhase(); return;
    }
    const bool jog = text.rfind("$J=",0)==0;
    if(text.front()=='$' && !jog) { reply(3); return; }
    if(state_==State::Alarm || (!jog && state_==State::Jog)) { reply(9); return; }
    if(jog && state_!=State::Idle && state_!=State::Jog) { reply(8); return; }
    // Parse into temporary modal state; errors cannot change any committed modal state.
    bool absolute=absolute_, inches=inches_;
    int mode=motion_;
    double feed=feed_;
    bool axes=false, machine=false, hasFeed=false, setZero=false, clearZero=false, dwell=false, end=false;
    bool spindleOn=spindleOn_;
    double spindleRpm=spindleRpm_;
    std::map<char,double> words;
    std::map<int,int> groups;
    const std::string code = jog ? text.substr(3) : text;
    for(std::size_t i=0;i<code.size();) {
        const char letter=code[i++];
        if(letter<'A' || letter>'Z') { reply(1); return; }
        const std::size_t start=i;
        if(i<code.size() && (code[i]=='+' || code[i]=='-')) ++i;
        bool dot=false, digit=false;
        while(i<code.size() && ((code[i]>='0' && code[i]<='9') || code[i]=='.')) {
            if(code[i]=='.') { if(dot) { reply(2); return; } dot=true; } else digit=true;
            ++i;
        }
        if(!digit) { reply(2); return; }
        const double value=std::strtod(code.substr(start,i-start).c_str(),nullptr);
        if(!std::isfinite(value) || std::abs(value)>1e9) { reply(2); return; }
        if(letter=='G') {
            if(!jog && value==92.1) {
                if(groups.count(4)) { reply(21); return; }
                groups[4]=921; clearZero=true; continue;
            }
            if(value!=std::floor(value)) { reply(20); return; }
            if(value<0 || value>10000) { reply(20); return; }
            const int g=static_cast<int>(value);
            int group=-1;
            if(g>=0 && g<=3) { if(jog) { reply(16); return; } group=1; mode=g; }
            else if(g==20 || g==21) { group=2; inches=(g==20); }
            else if(g==90 || g==91) { group=3; absolute=(g==90); }
            else if(g==53) { group=4; machine=true; }
            else if(!jog && g==92) { group=4; setZero=true; }
            else if(!jog && g==4) { group=4; dwell=true; }
            else if(!jog && (g==17 || g==54 || g==94)) group=g;
            else { reply(jog?16:20); return; }
            if(groups.count(group)) { reply(21); return; } groups[group]=g;
        } else if(letter=='M') {
            if(jog || value!=std::floor(value)) { reply(20); return; }
            int group=0;
            if(value==3 || value==5) { group=10; spindleOn=value!=5; }
            else if(value==9) group=11; // Coolant-off is a harmless shutdown command; M8 is unsupported.
            else if(value==2 || value==30) { group=12; end=true; }
            else { reply(20); return; }
            if(groups.count(group)) { reply(21); return; } groups[group]=static_cast<int>(value);
        } else {
            if(words.count(letter)) { reply(25); return; }
            if(letter!='X' && letter!='Y' && letter!='Z' && letter!='F' && letter!='N' && letter!='I' && letter!='J' && letter!='P' && letter!='S') { reply(20); return; }
            if(jog && letter!='X' && letter!='Y' && letter!='Z' && letter!='F' && letter!='N') { reply(16); return; }
            words[letter]=value;
            if(letter=='F') { if(value<=0) { reply(4); return; } hasFeed=true; }
            if(letter=='X' || letter=='Y' || letter=='Z') axes=true;
        }
    }
    if(words.count('N') && (words['N']<1 || words['N']>9999999 || words['N']!=std::floor(words['N']))) { reply(27); return; }
    const double scale=inches?25.4:1.0;
    for(char letter:std::string("XYZIJF")) if(words.count(letter) && (!std::isfinite(words[letter]*scale) || std::abs(words[letter]*scale)>1e9)) { reply(2); return; }
    if(hasFeed) feed=words['F']*scale;
    if(jog && (!hasFeed || !axes)) { reply(!hasFeed?22:26); return; }
    if(words.count('S')) { if(words['S']<0) { reply(4); return; } spindleRpm=words['S']; }
    if((setZero || clearZero || dwell || end || groups.count(10) || groups.count(11) || words.count('S')) && !blocks_.empty()) { synchronized_=text; return; }
    if(setZero && clearZero) { reply(21); return; }
    if((setZero || clearZero || dwell) && groups.count(1)) { reply(24); return; }
    if(dwell) {
        if(!words.count('P') || words['P']<0 || axes || words.count('I') || words.count('J')) { reply(28); return; }
        if(hasFeed || words.count('S') || groups.count(10) || groups.count(11) || end) { reply(36); return; }
        absolute_=absolute; inches_=inches;
        dwell_=words['P']; state_=State::Run; return;
    }
    if(words.count('P')) { reply(36); return; }
    if(setZero || clearZero) {
        if(words.count('I') || words.count('J') || words.count('S') || hasFeed || groups.count(10) || groups.count(11) || end || (setZero && !axes) || (clearZero && axes)) { reply(36); return; }
        if(clearZero) workOffset_={};
        else for(int i=0;i<3;++i) if(words.count("XYZ"[i])) workOffset_[i]=position_[i]-words["XYZ"[i]]*scale;
        absolute_=absolute; inches_=inches;
        reply(); return;
    }
    if(machine && (mode>1 || !absolute)) { reply(30); return; }
    if(end && (axes || words.count('I') || words.count('J'))) { reply(36); return; }
    if((axes || words.count('I') || words.count('J')) && (jog || mode>0) && feed<=0) { reply(22); return; }
    Position target=planned_;
    for(int i=0;i<3;++i) if(words.count("XYZ"[i])) {
        const double value=words["XYZ"[i]]*scale;
        target[i]=machine?value:(absolute?value+workOffset_[i]:target[i]+value);
    }
    if(!std::isfinite(feed) || std::abs(feed)>1e9 || std::any_of(target.begin(),target.end(),[](double v){return !std::isfinite(v) || std::abs(v)>1e9;})) { reply(2); return; }
    std::optional<Arc> arc;
    if(!jog && mode>=2 && (axes || words.count('I') || words.count('J'))) {
        if(!words.count('I') && !words.count('J')) { reply(35); return; }
        const Position center{planned_[0]+(words.count('I')?words['I']*scale:0),planned_[1]+(words.count('J')?words['J']*scale:0),planned_[2]};
        const double radius=std::hypot(planned_[0]-center[0],planned_[1]-center[1]);
        const double radiusEnd=std::hypot(target[0]-center[0],target[1]-center[1]);
        if(radius<1e-8 || std::abs(radius-radiusEnd)>std::max(0.005,radius*0.001)) { reply(33); return; }
        const double angle=std::atan2(planned_[1]-center[1],planned_[0]-center[0]);
        double sweep=std::atan2(target[1]-center[1],target[0]-center[0])-angle;
        constexpr double tau=6.283185307179586;
        if(mode==2) { while(sweep>=-1e-10) sweep-=tau; } else { while(sweep<=1e-10) sweep+=tau; }
        arc=Arc{planned_,center,angle,sweep,radius,std::hypot(radius*sweep,target[2]-planned_[2])};
        // Exact cardinal extrema validation; sampling alone could miss a narrow excursion.
        if(config_.softLimits) for(int i=0;i<4;++i) {
            const double theta=i*tau/4;
            double delta=sweep>0?theta-angle:angle-theta;
            while(delta<0) delta+=tau;
            while(delta>=tau) delta-=tau;
            if(delta>std::abs(sweep)+1e-9) continue;
            const double fraction=delta/std::abs(sweep);
            const Position p{center[0]+radius*std::cos(theta),center[1]+radius*std::sin(theta),planned_[2]+fraction*(target[2]-planned_[2])};
            if(!within(p)) { alarm(2); reply(); return; }
        }
    } else if(words.count('I') || words.count('J')) { reply(36); return; }
    if(axes && !within(target) && config_.softLimits) {
        if(jog) reply(15); else { alarm(2); reply(); }
        return;
    }
    if(!jog) { absolute_=absolute; inches_=inches; motion_=mode; feed_=feed; spindleOn_=spindleOn; spindleRpm_=spindleRpm; }
    if(end) { spindleOn_=false; absolute_=true; inches_=false; motion_=0; }
    if(arc || (axes && distance(target,planned_)>1e-9)) {
        blocks_.push_back({target,jog || mode>0?feed:100000,jog,arc,jog?4:mode}); planned_=target;
        if(state_==State::Idle) state_=jog?State::Jog:State::Run;
    }
    reply();
}
void Controller::startHomePhase() {
    // Healthy switches: Z positive end, then XY negative ends.
    // XY switch contacts are 2 mm before the chosen usable-area zero. Locate establishes
    // contact coordinates -pullOff; the final physical pull-off parks at machine XY0.
    // Homing alone may leave normal soft-limit bounds during switch seeking.
    Position target=position_;
    const int phase=homePhase_%4;
    if(homePhase_<4) target[2]=(phase==0 || phase==2)?0:-config_.pullOff;
    else for(int i=0;i<2;++i) target[i]=(phase==0 || phase==2)?-config_.pullOff:0;
    homeBlock_=Block{target,(phase==0?config_.homeSeek:config_.homeFeed),false};
    planned_=target;
}
void Controller::motion(double dt) {
    Block* block = state_==State::Home ? (homeBlock_?&*homeBlock_:nullptr) : (blocks_.empty()?nullptr:&blocks_.front());
    if(!block) return;
    const Position before=position_;
    const int kind=state_==State::Home?5:block->kind;
    const double remaining=block->arc ? block->arc->length-block->arc->progress : distance(position_,block->target);
    double maxSpeed=block->rate/60.0, acceleration=std::numeric_limits<double>::max();
    if(block->arc) {
        maxSpeed=std::min(maxSpeed,std::min(config_.maxRate[0],config_.maxRate[1])/60);
        acceleration=0.5*std::min(config_.acceleration[0],config_.acceleration[1]);
        maxSpeed=std::min(maxSpeed,std::sqrt(acceleration*block->arc->radius));
        const double zRatio=std::abs(block->target[2]-block->arc->start[2])/block->arc->length;
        if(zRatio>1e-9) { maxSpeed=std::min(maxSpeed,config_.maxRate[2]/60/zRatio); acceleration=std::min(acceleration,config_.acceleration[2]/zRatio); }
    } else if(remaining>1e-9) for(int i=0;i<3;++i) {
        const double component=std::abs(block->target[i]-position_[i])/remaining;
        if(component>1e-9) {
            maxSpeed=std::min(maxSpeed,config_.maxRate[i]/60.0/component);
            acceleration=std::min(acceleration,config_.acceleration[i]/component);
        }
    }
    if(remaining<=1e-9) acceleration=*std::min_element(config_.acceleration.begin(),config_.acceleration.end());
    const double desired=holding_?0:std::min(maxSpeed,std::sqrt(2*acceleration*remaining));
    const double next=velocity_+std::clamp(desired-velocity_,-acceleration*dt,acceleration*dt);
    const double step=std::min(remaining,(velocity_+next)*0.5*dt);
    velocity_=next;
    if(block->arc) {
        auto& arc=*block->arc; arc.progress+=step;
        const double fraction=std::min(1.0,arc.progress/arc.length),theta=arc.angle+arc.sweep*fraction;
        position_={arc.center[0]+arc.radius*std::cos(theta),arc.center[1]+arc.radius*std::sin(theta),arc.start[2]+fraction*(block->target[2]-arc.start[2])};
    } else if(remaining>1e-9) for(int i=0;i<3;++i) position_[i]+=(block->target[i]-position_[i])*step/remaining;
    if(state_!=State::Home && !within(position_)) { alarm(1); return; }
    if(observer_ && distance(before,position_)>1e-12) observer_({before,position_,kind,spindleOn_?spindleRpm_:0});
    if(holding_ && velocity_<1e-9) {
        if(cancelJog_) {
            blocks_.clear(); rx_.clear(); line_.clear(); planned_=position_;
            holding_=cancelJog_=overflow_=comment_=semicolon_=false; state_=State::Idle;
        } else state_=State::Hold;
        return;
    }
    if(step>=remaining-1e-9) {
        position_=block->target; velocity_=0;
        if(state_==State::Home) {
            // Slow switch location anchors the selected machine frame, not a DRO-only offset.
            if(homePhase_==2) { position_[2]=0; referencedAxes_[2]=true; }
            if(homePhase_==6) { position_[0]=position_[1]=-config_.pullOff; referencedAxes_[0]=referencedAxes_[1]=true; }
            if(++homePhase_==8) {
                homePhase_=-1; homeBlock_.reset(); homed_=true; planned_=position_; state_=State::Idle; reply();
            } else startHomePhase();
        } else {
            blocks_.pop_front(); ++executed_;
            if(cancelJog_) {
                blocks_.clear(); rx_.clear(); line_.clear(); planned_=position_;
                holding_=cancelJog_=overflow_=comment_=semicolon_=false; state_=State::Idle;
                return;
            }
            if(blocks_.empty()) {
                planned_=position_;
                if(holding_) { holding_=cancelJog_=false; }
                state_=State::Idle;
            }
        }
    }
}
void Controller::tick(double seconds) {
    if(!std::isfinite(seconds) || seconds<=0) return;
    // Caller clamps stalls; fine substeps keep the integrator deterministic.
    while(seconds>1e-9) {
        const double dt=std::min(seconds,0.002); seconds-=dt;
        if(dwell_>=0 && !holding_) { dwell_-=dt; if(dwell_<0) { dwell_=-1; state_=State::Idle; reply(); } }
        if(state_==State::Run || state_==State::Jog || state_==State::Home || (holding_ && state_!=State::Alarm)) motion(dt);
        drain();
    }
}
Snapshot Controller::snapshot() const {
    const std::array<bool,3> switches{position_[0]<=-config_.pullOff+1e-8,position_[1]<=-config_.pullOff+1e-8,position_[2]>=-1e-8};
    return {position_,planned_,workOffset_,state_,homed_,velocity_,blocks_.size(),rx_.size(),received_,accepted_,executed_,rejected_,homePhase_,spindleOn_?spindleRpm_:0,switches,referencedAxes_};
}
std::string Controller::status() const {
    std::string state=stateName(state_);
    if(state_==State::Hold) state+=":0";
    else if(holding_) state="Hold:1";
    std::ostringstream s; s.imbue(std::locale::classic());
    s << '<' << state << "|MPos:" << coordinates(position_) << "|Bf:" << plannerCapacity-blocks_.size()
      << ',' << rxCapacity-rx_.size() << "|FS:" << std::fixed << std::setprecision(1) << velocity_*60
      << ',' << (spindleOn_?spindleRpm_:0) << "|WCO:" << coordinates(workOffset_) << '>';
    return s.str();
}
std::vector<std::string> Controller::takeOutput() { auto result=std::move(output_); output_.clear(); return result; }
}
