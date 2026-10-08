#include "ui/program.h"
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
CarvingProgram analyzeProgram(const QString& text) {
    CarvingProgram result; result.suggested.toolDatum=148;result.suggested.resolution=.1;
    const auto header=text.left(8192);
    auto& tool=result.suggested.cutter;
    const QRegularExpression diameter(R"(([0-9]+(?:\.[0-9]+)?)\s*mm\s*(?:Ball|Flat|shank|球|平))",QRegularExpression::CaseInsensitiveOption);
    auto match=diameter.match(header);
    if(match.hasMatch()) { tool.diameter=match.captured(1).toDouble();result.toolIdentified=true; }
    else if(header.contains("1/8")) { tool.diameter=3.175;result.toolIdentified=true; }

    if(!result.toolIdentified) {
        const auto named=QRegularExpression(R"((?:Tool:[^\r\n]*?|;\s*T[0-9][^\r\n]*?)([0-9]+(?:\.[0-9]+)?)\s*mm)",QRegularExpression::CaseInsensitiveOption).match(header);
        if(named.hasMatch()) {tool.diameter=named.captured(1).toDouble();result.toolIdentified=true;}
    }
    if(header.contains("Ball",Qt::CaseInsensitive) || header.contains(QStringLiteral("球头"))) { tool.shape=cnc::CutterShape::Ball;result.toolIdentified=true; }
    if(header.contains("V-Bit",Qt::CaseInsensitive) || header.contains("Vbit",Qt::CaseInsensitive)) {
        tool.shape=cnc::CutterShape::VBit; result.toolIdentified=true;
        auto angle=QRegularExpression(R"(([0-9]+(?:\.[0-9]+)?)\s*deg)",QRegularExpression::CaseInsensitiveOption).match(header);
        if(angle.hasMatch()) tool.angle=angle.captured(1).toDouble();
        auto tip=QRegularExpression(R"(tip\s*([0-9]+(?:\.[0-9]+)?)\s*mm)",QRegularExpression::CaseInsensitiveOption).match(header);
        if(tip.hasMatch()) tool.tipDiameter=tip.captured(1).toDouble();
    }
    double thickness=0;
    const auto job=QRegularExpression(R"(JOB:\s*(\{[^\r\n]+\}))").match(header);
    if(job.hasMatch()) thickness=QJsonDocument::fromJson(job.captured(1).toUtf8()).object().value("z").toDouble();
    bool absolute=true,seenXY=false; double scale=1; int mode=0; cnc::Position pos{};
    const QRegularExpression word(R"(([A-Z])([+-]?(?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)))");
    int sourceLine=0;
    auto error=[&](const QString& s) {throw std::runtime_error(QStringLiteral("第 %1 行：%2").arg(sourceLine).arg(s).toUtf8().constData());};
    for(const auto& original:text.split('\n')) {
        ++sourceLine; QString code; bool comment=false;
        for(QChar c:original) {
            if(!comment && c==';') break;
            if(c=='(') { if(comment) error(QStringLiteral("嵌套注释不支持"));comment=true;continue; }
            if(c==')') { if(!comment) error(QStringLiteral("注释括号不匹配"));comment=false;continue; }
            if(!comment && !c.isSpace()) code+=c.toUpper();
        }
        if(comment) error(QStringLiteral("注释括号不匹配"));
        if(code.isEmpty() || code=="%") continue;
        if(code.toUtf8()!=code.toLatin1() || code.size()>79) error(QStringLiteral("命令必须为 ASCII 且不超过 79 字符；注释可用中文"));
        std::map<char,double> values; bool axes=false,coordinate=false;
        int consumed=0;
        auto words=word.globalMatch(code);
        while(words.hasNext()) {
            const auto w=words.next(); if(w.capturedStart()!=consumed) error(QStringLiteral("无法解析命令")); consumed=w.capturedEnd();
            const char c=w.captured(1).toLatin1()[0]; const double v=w.captured(2).toDouble();
            if(!std::isfinite(v) || std::abs(v)>1e9) error(QStringLiteral("数值超出范围"));
            if(c=='G') {
                if(v>=0 && v<=3 && v==std::floor(v)) mode=static_cast<int>(v);
                else if(v==20 || v==21) scale=v==20?25.4:1;
                else if(v==90 || v==91) {absolute=v==90;if(v==91) result.automatic=false;}
                else if(v==53 || v==92 || v==92.1) {result.automatic=false;coordinate=true;}
                else if(v!=4 && v!=17 && v!=54 && v!=94) error(QStringLiteral("当前不支持 G%1").arg(v));
            } else if(c=='M') {
                if(v!=3 && v!=5 && v!=9 && v!=2 && v!=30) error(QStringLiteral("当前不支持 M%1，需拆分换刀程序").arg(v));
            } else {
                if(QStringLiteral("XYZIJFNSP").indexOf(QChar(c))<0) error(QStringLiteral("当前不支持 %1").arg(QChar(c)));
                values[c]=v; axes|=c=='X'||c=='Y'||c=='Z';
            }
        }
        if(consumed!=code.size()) error(QStringLiteral("无法解析命令"));
        result.lines.push_back(code.toStdString());
        if(coordinate || !axes) continue;
        const auto before=pos;
        for(int i=0;i<3;++i) if(values.count("XYZ"[i])) pos[i]=absolute?values["XYZ"[i]]*scale:pos[i]+values["XYZ"[i]]*scale;
        result.minZ=std::min(result.minZ,pos[2]);result.maxZ=std::max(result.maxZ,pos[2]);
        if(values.count('X') || values.count('Y')) {
            if(!seenXY) {result.minX=result.maxX=pos[0];result.minY=result.maxY=pos[1];seenXY=true;}
            result.minX=std::min(result.minX,pos[0]);result.maxX=std::max(result.maxX,pos[0]);
            result.minY=std::min(result.minY,pos[1]);result.maxY=std::max(result.maxY,pos[1]);
        }
        if(mode>=2 && (values.count('I') || values.count('J'))) {
            const double cx=before[0]+values['I']*scale,cy=before[1]+values['J']*scale,r=std::hypot(before[0]-cx,before[1]-cy);
            // Conservative full circle includes every possible arc extremum.
            result.minX=std::min(result.minX,cx-r);result.maxX=std::max(result.maxX,cx+r);
            result.minY=std::min(result.minY,cy-r);result.maxY=std::max(result.maxY,cy+r);
        }
    }
    if(result.lines.empty() || !seenXY) throw std::runtime_error("G-code has no XY motion");
    if(!result.automatic) {result.suggested={};result.summary=QStringLiteral("%1 条命令；包含机器坐标/重设零点，使用手动加工设置").arg(result.lines.size());return result;}
    auto& s=result.suggested;
    const double margin=std::max(3.0,tool.diameter/2+1);
    s.width=std::max(20.0,result.maxX-result.minX+2*margin);s.depth=std::max(20.0,result.maxY-result.minY+2*margin);
    if(s.width>300 || s.depth>180) throw std::runtime_error("G-code XY envelope with cutter exceeds 300 x 180 mm");
    s.workX=150-(result.minX+result.maxX)/2;s.workY=90-(result.minY+result.maxY)/2;
    s.x=(300-s.width)/2;s.y=(180-s.depth)/2;
    s.thickness=thickness>=1?thickness:std::max(10.0,std::ceil(-result.minZ+2));
    tool.cuttingLength=std::max(12.0,-result.minZ+1);tool.stickout=std::max(22.0,tool.cuttingLength+4);
    if(s.thickness>60 || tool.stickout>40 || s.bottom+s.thickness+result.minZ-s.toolDatum<-80 || s.bottom+s.thickness+result.maxZ-s.toolDatum>0)
        throw std::runtime_error("Z envelope requires a different stock/tool setup");
    const QString shape=tool.shape==cnc::CutterShape::Ball?QStringLiteral("球刀"):tool.shape==cnc::CutterShape::VBit?QStringLiteral("V刀"):QStringLiteral("平刀");
    result.summary=QStringLiteral("%1 条命令 · %2 %3 mm%4 · 建议木板 %5×%6×%7 mm · 最深 %8 mm").arg(result.lines.size()).arg(shape).arg(tool.diameter).arg(result.toolIdentified?QStringLiteral("（文件候选）"):QStringLiteral("（默认，需核对）")).arg(s.width,0,'f',1).arg(s.depth,0,'f',1).arg(s.thickness).arg(-result.minZ);
    return result;
}
