#pragma once
#include "core/stock.h"
#include <QString>
#include <vector>
struct CarvingProgram {
    std::vector<std::string> lines;
    cnc::StockSetup suggested;
    double minZ=0,maxZ=0,minX=0,maxX=0,minY=0,maxY=0;
    bool automatic=true,toolIdentified=false;
    QString summary;
};
CarvingProgram analyzeProgram(const QString& text);
