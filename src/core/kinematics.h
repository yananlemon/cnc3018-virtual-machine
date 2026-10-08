#pragma once
#include "core/controller.h"

namespace cnc {
// Setup geometry from the independent Lunyee profile, not a manufacturer machine-zero datum.
struct Kinematics3018 {
    using Profile=Lunyee3018ProUltra;
    static constexpr double xCenter=Profile::xCenter, yCenter=Profile::yCenter, zDatum=Profile::zDatum, workpieceTop=Profile::workpieceTop;
    static Position toolWorld(const Position& machine) { return {machine[0]-xCenter,machine[2]+zDatum,Profile::cutPlaneZ}; }
    static double tableWorldZ(const Position& machine) { return yCenter-machine[1]; }
    static Position toolTableLocal(const Position& machine) { return {machine[0]-xCenter,machine[2]+zDatum,machine[1]-yCenter}; }
};
}
