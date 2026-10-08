#include "ui/setup_document.h"
#include <iostream>
#include <stdexcept>
void require(bool b,const char* s) {if(!b) throw std::runtime_error(s);}
int main() {
try {
    WorkspaceSetup original;original.keepStock=true;original.fileTool=false;original.stock.x=60;original.stock.y=30;original.stock.width=180;original.stock.depth=100;original.stock.thickness=12;
    original.fixtures.automatic=false;original.fixtures.clamps[0]={true,true,48,80,0,32,16,12};original.fixtures.clamps[1]={true,false,252,80,180,40,18,15};
    const auto json=encodeSetup(original);const auto decoded=decodeSetup(json);require(encodeSetup(decoded)==json,"exact setup JSON round trip");
    auto shifted=original.stock;shifted.x+=10;shifted.y+=5;
    const auto manual=cnc::placedFixtures(original.fixtures,shifted);require(manual.clamps[0].x==48 && manual.clamps[0].y==80,"manual fixture placement independent of stock move/import");
    cnc::FixtureSetup automatic;const auto placed=cnc::placedFixtures(automatic,shifted);require(placed.clamps[0].x==shifted.x-12 && placed.clamps[1].x==shifted.x+shifted.width+12,"automatic fixtures follow stock");
    auto bad=json;auto s=bad.value("stock").toObject();s["width"]=400;bad["stock"]=s;bool rejected=false;try {decodeSetup(bad);}catch(const std::exception&) {rejected=true;}require(rejected,"reject out-of-table stock without partial apply");
    auto fixture=original.fixtures;fixture.clamps[0].length=-10;rejected=false;try {cnc::validateFixtures(fixture);}catch(const std::exception&) {rejected=true;}require(rejected,"reject invalid clamp geometry");
    std::cout<<"setup persistence and fixture contract passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
