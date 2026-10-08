#pragma once
#include "ui/fixture_setup.h"
#include <QJsonObject>
struct WorkspaceSetup {
    cnc::StockSetup stock;
    cnc::FixtureSetup fixtures;
    bool keepStock=false,fileTool=true;
};
QJsonObject encodeSetup(const WorkspaceSetup& setup);
WorkspaceSetup decodeSetup(const QJsonObject& object);
