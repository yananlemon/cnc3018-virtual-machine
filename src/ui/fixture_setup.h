#pragma once
#include "core/stock.h"
#include <array>
#include <cmath>
#include <stdexcept>
namespace cnc {
struct ClampSetup {
    bool enabled=false,followSurface=true;
    double x=0,y=0,angle=0,length=32,width=16,contactHeight=14;
};
struct FixtureSetup {
    bool automatic=true;
    std::array<ClampSetup,4> clamps{};
};
inline FixtureSetup placedFixtures(FixtureSetup setup,const StockSetup& stock) {
    if(!setup.automatic) return setup;
    setup.clamps={};
    setup.clamps[0]={true,true,stock.x-12,stock.y+stock.depth/2,0,32,16,stock.thickness};
    setup.clamps[1]={true,true,stock.x+stock.width+12,stock.y+stock.depth/2,180,32,16,stock.thickness};
    return setup;
}
inline void validateFixtures(const FixtureSetup& setup) {
    for(const auto& c:setup.clamps) {
        for(double v:{c.x,c.y,c.angle,c.length,c.width,c.contactHeight}) if(!std::isfinite(v)) throw std::invalid_argument("Non-finite fixture parameter");
        if(c.x<-15 || c.x>315 || c.y<-15 || c.y>195 || c.angle<-360 || c.angle>360 || c.length<20 || c.length>100 || c.width<8 || c.width>50 || c.contactHeight<1 || c.contactHeight>60)
            throw std::invalid_argument("Fixture dimensions or table position outside supported range");
    }
}
}
