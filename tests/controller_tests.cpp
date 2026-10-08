#include "core/controller.h"
#include "core/streamer.h"
#include "core/kinematics.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
std::string output(cnc::Controller& c) { std::string s; for(auto& x:c.takeOutput()) s+=x; return s; }
void settle(cnc::Controller& c,double seconds=100) { for(int i=0;i<seconds*100;++i) c.tick(0.01); }
void unlock(cnc::Controller& c) { c.ingest("$X\n"); output(c); }
bool close(double a,double b) { return std::abs(a-b)<1e-5; }
void tests() {
    using cnc::State;
    cnc::Controller c;
    c.connect(); require(output(c).find("Grbl 1.1h")!=std::string::npos,"startup banner");
    const auto initial=c.snapshot().position;
    c.ingest("G1X-100F300\n"); require(output(c)=="error:9\r\n","startup alarm rejects motion");
    unlock(c); require(!c.snapshot().homed,"unlock must not home");
    require(c.snapshot().position==initial,"unlock must not move");
    c.ingest("G91G1X10F600\n"); require(output(c)=="ok\r\n","motion accepted");
    require(c.snapshot().position==initial && c.snapshot().executed==0,"ack is not execution");
    c.tick(0.01); require(c.snapshot().velocity>0 && c.snapshot().velocity<10,"acceleration ramp");
    c.ingest("!"); require(output(c).empty(),"hold has no line ack");
    c.tick(0.2); require(c.snapshot().state==State::Hold,"hold deceleration");
    const auto stopped=c.snapshot().position; c.tick(1); require(stopped==c.snapshot().position,"hold retains position");
    c.ingest("?"); require(output(c).find("<Hold:0|MPos:")!=std::string::npos,"status without newline");
    c.ingest("~"); settle(c,3); require(c.snapshot().state==State::Idle && close(c.snapshot().position[0],initial[0]+10),"resume completes target");
    require(c.snapshot().executed==1,"execution counter only on completed block");
    c.ingest("G90G18X-100\n"); require(output(c)=="error:20\r\n","unsupported plane rejected");
    c.ingest("G1X1F600\n"); output(c); settle(c,2);
    require(close(c.snapshot().position[0],initial[0]+11),"invalid block cannot commit G90 mode");
    c.ingest("$J=G91X20F600\n"); require(output(c)=="ok\r\n","jog accepted");
    c.tick(0.2); c.ingest(std::string(1,static_cast<char>(0x85))); c.tick(1);
    require(c.snapshot().state==State::Idle && c.snapshot().queueDepth==0,"jog cancel purges queue");
    c.ingest("G1X1F300\n"); output(c); const auto jogEnd=c.snapshot().position;
    settle(c,2); require(close(c.snapshot().position[0],jogEnd[0]+1),"jog preserves parser and rebases planned target");
    c.ingest("$J=G90X-1F100\n"); require(output(c)=="error:15\r\n","jog out of travel is rejected without alarm");
    c.ingest("G90G1X-1F100\n"); require(output(c).find("ALARM:2")!=std::string::npos,"soft limit alarm");
    c.ingest("$H\n"); require(output(c).empty(),"homing ack deferred");
    c.tick(0.1); require(c.snapshot().state==State::Home && !c.snapshot().homed,"homing takes time");
    c.ingest("?"); require(output(c).empty(),"original profile suppresses status during homing");
    settle(c,60); require(c.snapshot().homed && c.snapshot().state==State::Idle,"homing completion");
    require(c.snapshot().position==cnc::Position{0,0,-2},"homing final pulloff");
    require(output(c)=="ok\r\n","single homing ack");
    c.hardLimit(0); require(output(c)=="ALARM:1\r\n" && !c.snapshot().homed,"hard limit invalidates reference");
    unlock(c); c.ingest("G91G1X20F300\n"); output(c); c.tick(0.1);
    c.ingest(std::string(1,static_cast<char>(0x18))); auto reset=output(c);
    require(reset.find("Grbl 1.1h")!=std::string::npos && reset.find("ALARM:3")!=std::string::npos,"reset during movement");
    require(c.snapshot().queueDepth==0 && c.snapshot().rxBytes==0,"reset clears buffers");

    cnc::Controller stream; unlock(stream);
    for(int i=0;i<15;++i) { stream.ingest("G91G1X-1F60\n"); output(stream); }
    require(stream.snapshot().queueDepth==15,"planner bound");
    stream.ingest("G1X-1\n"); require(output(stream).empty(),"full planner defers ack");
    require(stream.snapshot().rxBytes>0,"pending line in RX");
    stream.ingest("?"); require(output(stream).find("|Bf:0,")!=std::string::npos,"realtime bypasses full planner");
    settle(stream,30); require(stream.snapshot().executed==16,"stream progresses without loss");
    require(close(stream.snapshot().position[0],134),"incremental queued endpoints");

    cnc::Controller framing; unlock(framing);
    framing.ingest("g91g1x-1?f60 (comment)\n"); auto mixed=output(framing);
    require(mixed.find("<Idle|")!=std::string::npos && mixed.find("ok\r\n")!=std::string::npos,"realtime inside partial command");
    settle(framing,3); require(close(framing.snapshot().position[0],149),"comment and lowercase handling");
    framing.ingest("G1X1X2\n"); require(output(framing)=="error:25\r\n","duplicate words");
    framing.ingest(std::string(80,'A')+"\n"); require(output(framing)=="error:11\r\n","line length bound");
    framing.ingest("G1X\n"); require(output(framing)=="error:2\r\n","missing numeric value");
    framing.ingest(std::string(128,'A')); require(framing.snapshot().state==State::Alarm,"RX overrun bounded");
    output(framing);
    framing.ingest(std::string(1,static_cast<char>(0x18))); require(framing.snapshot().rxBytes==0,"realtime reset recovers RX overrun");
    cnc::Controller homeAbort; homeAbort.ingest("$H\n"); homeAbort.tick(0.1);
    homeAbort.ingest(std::string(1,static_cast<char>(0x18)));
    require(homeAbort.snapshot().state==State::Alarm && !homeAbort.snapshot().homed,"homing abort never sets reference");
}
void coordinatesAndArcs() {
    cnc::Controller c; unlock(c);
    c.ingest("G92X0Y0Z0\n"); require(output(c)=="ok\r\n","set work zero");
    require(c.snapshot().workOffset==cnc::Position{150,90,-40},"work offset from actual position");
    c.ingest("G90G1X10Y0F600\n"); output(c); settle(c,3);
    require(close(c.snapshot().position[0],160),"G54 work coordinate movement");
    c.ingest("G3X0Y10I-10J0F600\n"); require(output(c)=="ok\r\n","CCW IJ arc accepted");
    c.tick(0.5); require(c.snapshot().position[0]<160 && c.snapshot().position[1]>90,"arc intermediate point is curved");
    settle(c,5); require(close(c.snapshot().position[0],150) && close(c.snapshot().position[1],100),"arc endpoint");
    c.ingest("G2X10Y0I0J-10F600\n"); require(output(c)=="ok\r\n","CW arc accepted"); settle(c,5);
    require(close(c.snapshot().position[0],160) && close(c.snapshot().position[1],90),"CW endpoint");
    c.ingest("G2X10Y0I-10J0\n"); output(c); settle(c,10);
    require(close(c.snapshot().position[0],160),"full circle completes");
    c.ingest("G3X0Y10Z-5I-10J0F600\n"); require(output(c)=="ok\r\n","helical arc accepted"); settle(c,5);
    require(close(c.snapshot().position[2],-45) && close(c.snapshot().position[0],150),"helical interpolation endpoint");
    c.ingest("G1X1I2\n"); require(output(c)=="error:36\r\n","unused arc word rejected");
    c.ingest("G2X10Y0I1J0\n"); require(output(c)=="error:33\r\n","invalid radius rejected");
    c.ingest("G53G0X170Y95\n"); output(c); settle(c,5);
    require(close(c.snapshot().position[0],170),"G53 bypasses work offset");
    c.ingest("G4P0.5\n"); require(output(c).empty(),"dwell ack deferred"); c.tick(0.2);
    c.ingest("!"); c.tick(1); require(output(c).empty(),"dwell paused by hold");
    c.ingest("~"); c.tick(0.5); require(output(c)=="ok\r\n","dwell resumes");
    c.ingest("M3S8000\n"); output(c); require(c.snapshot().spindle==8000,"spindle state");
    c.ingest("M30\n"); output(c); require(c.snapshot().spindle==0,"program end stops spindle");
    c.ingest("G92.1\n"); output(c); require(c.snapshot().workOffset==cnc::Position{},"clear temporary offset");
}
void senderTests() {
    cnc::Streamer s;
    s.start({std::string(60,'A'),std::string(60,'B'),std::string(20,'C')});
    require(s.next().has_value() && s.next().has_value() && !s.next(),"byte budget gates sends");
    require(s.inflight()==122 && s.confirmed()==0,"sending does not release credit");
    s.acknowledge(true); require(s.next().has_value() && s.inflight()==82,"ack releases exact line bytes");
    s.stopSending(); require(s.active() && s.inflight()==82,"stop preserves pending ledger");
    s.acknowledge(true); s.acknowledge(true); require(!s.active() && s.confirmed()==3,"outstanding responses drain after stop");
    s.start({"G1X1","G1X2","G1X3"}); s.next(); s.acknowledge(false);
    require(!s.next() && s.errors()==1,"error stops subsequent sending");
    s.clear(); require(s.inflight()==0 && s.total()==0 && !s.active(),"reset clears ledger");
    bool rejected=false; try{s.start({std::string(80,'X')});}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"oversize line rejected before streaming");
}
void numericTests() {
    using P=cnc::Lunyee3018ProUltra;
    const cnc::Position surface{150,90,P::surfaceMachineZ};
    require(close(cnc::Kinematics3018::toolWorld(surface)[1],P::workpieceTop),"profile surface maps to workpiece top");
    require(close(cnc::Kinematics3018::toolWorld({150,90,P::surfaceMachineZ-2})[1],P::workpieceTop-2),"2 mm cut goes below wood surface");
    require(close(cnc::Kinematics3018::toolWorld({150,90,0})[1]-cnc::Kinematics3018::toolWorld({150,90,-80})[1],80),"nominal Z80 preserved by model transform");
    require(close(cnc::Kinematics3018::toolWorld({150,90,-80})[1],P::workpieceTop-P::penetrationReserve),"lowest tool height respects setup reserve");
    const cnc::Position yMotion{150,130,P::surfaceMachineZ};
    require(close(cnc::Kinematics3018::toolTableLocal(yMotion)[2]+cnc::Kinematics3018::tableWorldZ(yMotion),0),"table-local trail tracks inverse Y table transform");
    cnc::Controller c; unlock(c);
    for(auto line:{"G999999999999999999999999","G1X99999999999999999999999F100","G1X1e309F10","G1X..2","G1X1F999999999999999999999"}) {
        c.ingest(std::string(line)+"\n"); require(output(c).rfind("error:",0)==0,"invalid/huge numeric input rejected");
        require(c.snapshot().state==cnc::State::Idle && c.snapshot().queueDepth==0,"invalid input cannot enqueue motion");
    }
    c.ingest("G20G1X999999999F10\n"); require(output(c).rfind("error:",0)==0,"inch conversion range validated");
    c.ingest("G21G91G1Z-1F600\n"); output(c); c.tick(0.1);
    require(c.snapshot().velocity<=400.0/60+1e-8,"Z max speed constrains feed");
    cnc::Controller boundary; unlock(boundary);
    boundary.ingest("G53G0Z-80\n"); output(boundary); settle(boundary,20);
    require(boundary.snapshot().state==cnc::State::Idle && close(boundary.snapshot().position[2],-80),"nominal Z lower endpoint reachable");
    boundary.ingest("G53G0Z0\n"); output(boundary); settle(boundary,20);
    require(close(boundary.snapshot().position[2],0),"nominal Z upper endpoint reachable");
    boundary.ingest("G53G0Z-80.001\n"); require(output(boundary).find("ALARM:2")!=std::string::npos,"outside nominal Z80 triggers soft limit");
}
void originTests() {
    using cnc::State; using K=cnc::Kinematics3018;
    cnc::Controller c;
    c.ingest("$H\n");
    std::array<bool,8> seen{};
    bool zTriggered=false,xyTriggered=false,zLocated=false,xyLocated=false;
    for(int i=0;i<100000 && c.snapshot().state==State::Home;++i) {
        c.tick(.002); const auto s=c.snapshot();
        if(s.homePhase>=0) seen[static_cast<std::size_t>(s.homePhase)]=true;
        if(s.homePhase==1 && s.homeSwitches[2]) zTriggered=true;
        if(s.homePhase==3 && s.homeSwitches[2] && s.referencedAxes[2]) zLocated=true;
        if(s.homePhase==5 && s.homeSwitches[0] && s.homeSwitches[1]) xyTriggered=true;
        if(s.homePhase==7 && s.homeSwitches[0] && s.homeSwitches[1] && s.referencedAxes[0] && s.referencedAxes[1]) xyLocated=true;
    }
    require(zTriggered && xyTriggered && zLocated && xyLocated,"healthy switches trigger and slowly establish datum before final physical pull-off");
    for(bool phase:seen) require(phase,"all eight homing phases traversed");
    const auto home=c.snapshot();
    require(home.homed && home.state==State::Idle && home.position==cnc::Position{0,0,-2} && home.planned==home.position,"XY parking datum zero and Z 2mm pull-off");
    require(home.homeSwitches==std::array<bool,3>{} && home.referencedAxes==std::array<bool,3>{true,true,true},"all switches released after homing");
    require(output(c)=="ok\r\n","homing only acknowledges once after physical completion");
    const auto origin=K::toolTableLocal(home.position);
    require(close(origin[0],-150) && close(origin[2],-90),"XY0 located at usable-table back-left corner");
    require(K::toolWorld({5,0,-2})[0]>K::toolWorld({0,0,-2})[0],"X+ moves tool physically right");
    require(K::tableWorldZ({0,5,-2})<K::tableWorldZ({0,0,-2}),"Y+ moves table backward");
    require(K::toolTableLocal({0,5,-2})[2]>origin[2],"Y+ moves cutter relative to work physically forward/screen down");
    for(auto line:{"$J=G21G91X-5F500","$J=G21G91Y-5F500","$J=G21G91Z5F500"}) {
        c.ingest(std::string(line)+"\n"); require(output(c)=="error:15\r\n","home outward jog rejected");
        require(c.snapshot().position==home.position && c.snapshot().planned==home.position && c.snapshot().homed,"rejected jog preserves actual/planned position and reference");
    }
    c.ingest("$J=G21G91X5F500\n"); require(output(c)=="ok\r\n","X+5 from home accepted"); settle(c,2);
    require(c.snapshot().position==cnc::Position{5,0,-2},"X jog executes positive travel");
    c.ingest("$J=G21G91Y5F500\n"); require(output(c)=="ok\r\n","Y+5 from home accepted"); settle(c,2);
    require(c.snapshot().position==cnc::Position{5,5,-2},"Y jog executes positive travel");
    c.ingest("$J=G21G91Z-5F300\n"); require(output(c)=="ok\r\n","Z lowering allowed"); settle(c,2);
    require(c.snapshot().position==cnc::Position{5,5,-7},"Z down executes");
    c.ingest("$J=G21G91Z5F300\n"); output(c); settle(c,2);
    c.ingest("G92X0Y0\n"); output(c);
    require(c.snapshot().position==cnc::Position{5,5,-2} && c.snapshot().workOffset==cnc::Position{5,5,0},"G92 changes only work coordinates");
    c.ingest("G90G1X10Y10F600\n"); output(c); settle(c,3);
    require(c.snapshot().position==cnc::Position{15,15,-2},"work move uses offset with positive machine bounds");
    c.ingest("G53G0X300Y180\n"); output(c); settle(c,40);
    require(c.snapshot().state==State::Idle && c.snapshot().position==cnc::Position{300,180,-2},"opposite XY endpoints reachable");
    c.ingest("$J=G91X.001F100\n"); require(output(c)=="error:15\r\n","upper XY jog bound");
    c.ingest("$H\n"); settle(c,100); output(c);
    require(c.snapshot().position==cnc::Position{0,0,-2} && c.snapshot().workOffset==cnc::Position{5,5,0},"Home resets machine frame without silently changing G92");
    c.ingest("G53G0Y-0.001\n"); require(output(c).find("ALARM:2")!=std::string::npos,"negative Y normal move triggers soft limit");
    // Keep G2/G3 numeric angle semantics. Mirroring display Y changes apparent handedness.
    for(bool clockwise:{true,false}) {
        cnc::Controller arc; unlock(arc); arc.ingest("G0X160Y90\n"); output(arc); settle(arc,2);
        arc.ingest(clockwise?"G2X150Y80I-10J0F600\n":"G3X150Y100I-10J0F600\n"); require(output(arc)=="ok\r\n","arc in selected XY frame accepted"); arc.tick(.5);
        const auto s=arc.snapshot(); const auto local=K::toolTableLocal(s.position);
        require(s.position[0]<160 && (clockwise?s.position[1]<90:s.position[1]>90),"G2 negative angle / G3 positive angle preserved");
        require((clockwise?local[2]<0:local[2]>0),"top-view numeric arc reflection explicitly consistent with Y-down display");
        settle(arc,5); require(close(arc.snapshot().position[1],clockwise?80:100),"arc endpoint follows selected coordinates");
    }
}
void demoTests(const char* path) {
    std::ifstream file(path); require(file.is_open(),"actual distributed demo fixture opens");
    std::vector<std::string> lines; std::string line;
    while(std::getline(file,line)) {
        if(!line.empty() && line.back()=='\r') line.pop_back();
        if(!line.empty() && line.front()!=';') lines.push_back(line);
    }
    require(lines.size()==17,"complete demo fixture");
    cnc::Controller c; c.ingest("$H\n"); settle(c,60); output(c);
    c.ingest("G92X-10Y-20Z0\n"); require(output(c)=="ok\r\n","seed nonzero prior work offset");
    require(c.snapshot().workOffset==cnc::Position{10,20,-2},"nonzero work offset exists before first demo");
    for(int run=0;run<3;++run) {
        if(run==2) {
            const auto offset=c.snapshot().workOffset;
            c.ingest("$H\n"); settle(c,100); output(c);
            require(c.snapshot().position==cnc::Position{0,0,-2} && c.snapshot().workOffset==offset,"repeat after Home preserving work offset");
        }
        cnc::Streamer sender; sender.start(lines); const auto before=c.snapshot().executed;
        bool completed=false;
        for(int i=0;i<24000;++i) {
            while(auto next=sender.next()) c.ingest(*next);
            for(const auto& message:c.takeOutput()) {
                require(message.rfind("ALARM:",0)!=0,"demo must not alarm under prior G92");
                if(message=="ok\r\n") sender.acknowledge(true);
                if(message.rfind("error:",0)==0) sender.acknowledge(false);
            }
            c.tick(.025); const auto s=c.snapshot();
            require(sender.errors()==0,"demo stream has no error");
            if(!sender.active() && sender.confirmed()==lines.size() && s.state==cnc::State::Idle && s.queueDepth==0 && s.rxBytes==0) { completed=true; break; }
        }
        require(completed && c.snapshot().executed-before==12,"first/repeated/after-Home demo completes all motion");
        require(c.snapshot().position==cnc::Position{135,90,-65},"demo final actual machine position independent of initial G92");
        require(c.snapshot().workOffset==cnc::Position{120,65,-70},"demo establishes its own work offset after G53 approach");
    }
}
}
int main(int argc,char** argv) {
    try {
        const std::string suite=argc>1?argv[1]:"all";
        if(suite=="all" || suite=="protocol") tests();
        if(suite=="all" || suite=="coordinates") coordinatesAndArcs();
        if(suite=="all" || suite=="streamer") senderTests();
        if(suite=="all" || suite=="numeric") numericTests();
        if(suite=="all" || suite=="origin") originTests();
        if(suite=="demo") { require(argc>2,"demo fixture path required"); demoTests(argv[2]); }
        std::cout << "PASS: " << suite << '\n'; return 0;
    }
    catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
