#include "ui/setup_document.h"
#include <QJsonArray>
#include <stdexcept>
QJsonObject encodeSetup(const WorkspaceSetup& value) {
    const auto& s=value.stock;const auto& t=s.cutter;
    QJsonArray clamps;for(const auto& c:value.fixtures.clamps) clamps.append(QJsonObject{{"enabled",c.enabled},{"followSurface",c.followSurface},{"x",c.x},{"y",c.y},{"angle",c.angle},{"length",c.length},{"width",c.width},{"contactHeight",c.contactHeight}});
    return {{"schema",1},{"keepStock",value.keepStock},{"fileTool",value.fileTool},
        {"stock",QJsonObject{{"x",s.x},{"y",s.y},{"width",s.width},{"depth",s.depth},{"thickness",s.thickness},{"bottom",s.bottom},{"toolDatum",s.toolDatum},{"resolution",s.resolution},{"workX",s.workX},{"workY",s.workY}}},
        {"tool",QJsonObject{{"shape",static_cast<int>(t.shape)},{"diameter",t.diameter},{"angle",t.angle},{"tipDiameter",t.tipDiameter},{"cuttingLength",t.cuttingLength},{"stickout",t.stickout}}},
        {"fixtures",QJsonObject{{"automatic",value.fixtures.automatic},{"clamps",clamps}}}};
}
WorkspaceSetup decodeSetup(const QJsonObject& object) {
    if(object.value("schema").toInt()!=1 || !object.value("stock").isObject() || !object.value("tool").isObject() || !object.value("fixtures").isObject()) throw std::invalid_argument("Invalid setup document/schema");
    WorkspaceSetup result;result.keepStock=object.value("keepStock").toBool();result.fileTool=object.value("fileTool").toBool(true);
    auto number=[](const QJsonObject& o,const char* key) {if(!o.value(key).isDouble()) throw std::invalid_argument("Setup number missing/invalid");return o.value(key).toDouble();};
    const auto s=object.value("stock").toObject();auto& stock=result.stock;
    stock.x=number(s,"x");stock.y=number(s,"y");stock.width=number(s,"width");stock.depth=number(s,"depth");stock.thickness=number(s,"thickness");stock.bottom=number(s,"bottom");stock.toolDatum=number(s,"toolDatum");stock.resolution=number(s,"resolution");stock.workX=number(s,"workX");stock.workY=number(s,"workY");
    const auto t=object.value("tool").toObject();auto& tool=stock.cutter;const double shape=number(t,"shape");
    if(shape<0 || shape>2 || shape!=std::floor(shape)) throw std::invalid_argument("Invalid cutter shape");
    tool.shape=static_cast<cnc::CutterShape>(static_cast<int>(shape));tool.diameter=number(t,"diameter");tool.angle=number(t,"angle");tool.tipDiameter=number(t,"tipDiameter");tool.cuttingLength=number(t,"cuttingLength");tool.stickout=number(t,"stickout");
    const auto f=object.value("fixtures").toObject();result.fixtures.automatic=f.value("automatic").toBool();const auto clamps=f.value("clamps").toArray();
    if(clamps.size()!=4) throw std::invalid_argument("Setup requires four fixture slots");
    for(int i=0;i<4;++i) {
        const auto c=clamps[i].toObject();auto& clamp=result.fixtures.clamps[static_cast<std::size_t>(i)];
        clamp.enabled=c.value("enabled").toBool();clamp.followSurface=c.value("followSurface").toBool();clamp.x=number(c,"x");clamp.y=number(c,"y");clamp.angle=number(c,"angle");clamp.length=number(c,"length");clamp.width=number(c,"width");clamp.contactHeight=number(c,"contactHeight");
    }
    cnc::Stock validation(stock);cnc::validateFixtures(result.fixtures);return result;
}
