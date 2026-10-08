#include "ui/program.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
void require(bool v,const char* s) {if(!v) throw std::runtime_error(s);}
int main() {
try {
    const auto p=analyzeProgram(QStringLiteral("; Tool: T1 - 6.35mm\nG21 G90\nG0 Z5 (抬刀)\nM3 S12000\nG0 X10 Y10\nG1 Z-2 F200 ; 下刀\nG1 X30 F800\nG0 Z5\nM5\nM30\n"));
    require(p.lines.size()==9 && p.automatic && p.toolIdentified,"comments/source normalization");require(std::abs(p.suggested.cutter.diameter-6.35)<1e-9,"tool-only diameter header");
    require(p.lines[1]=="G0Z5","Unicode inline comments removed before streaming");
    const auto ball=analyzeProgram("; Tool: 3.175mm Ball Nose\nG21G90\nG0Z5\nG0X-30Y-20\nG1Z-4F300\nG1X30Y20F600\n");
    require(ball.suggested.cutter.shape==cnc::CutterShape::Ball && ball.suggested.x>=0 && ball.suggested.y>=0,"negative-coordinate relief centered on physical table");
    const auto inch=analyzeProgram("G20G90\nG0Z.1\nG0X1Y1\nG1Z-.1F10\nG1X2Y2\n");require(std::abs(inch.maxX-50.8)<1e-9 && std::abs(inch.minZ+2.54)<1e-9,"inch envelope");
    require(!analyzeProgram("G21G91\nG0X1Y1\nG1X2F300\n").automatic,"relative program requires explicit setup");
    require(!analyzeProgram("G21G90\nG53G0X100Y50Z-65\nG92X0Y0Z5\nG1X20Y20Z-1F300\n").automatic,"machine/G92 program preserves explicit setup");
    bool rejected=false;try {analyzeProgram("G90\nG0X10Y10\nG38.2Z-10F300\n");}catch(const std::exception&){rejected=true;}require(rejected,"unsupported code rejected before sending");
    std::cout<<"program import contract passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
