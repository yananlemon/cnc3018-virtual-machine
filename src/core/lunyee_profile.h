#pragma once
#include <array>
#include <string_view>

namespace cnc {
enum class Evidence { OfficialSpecification, PhotoEstimate, SimulationDefault, NeedsMeasurement };
struct Dimension { double mm; Evidence evidence; };
// All dimensions are mm. Official means a published specification, not manufacturer CAD.
struct Lunyee3018ProUltra {
    static constexpr std::string_view name="LUNYEE 3018 Pro Ultra";
    static constexpr std::string_view productUrl="https://www.lunyeecnc.com/products/3018-pro-ultra-cnc-router-machine-for-diy-engraving-dreams";
    static constexpr std::array<double,3> travel{300,180,80}; // Current product page; older manual says Z78.
    static constexpr double spindleWatts=500, spindleDiameter=52, spindleMaxRpm=12000;
    static constexpr double advertisedMaxSpeed=5000; // System headline, not three calibrated axis limits.
    static constexpr std::array<double,3> simulationRates{1000,1000,400};
    static constexpr std::array<double,3> simulationAcceleration{100,100,60};
    static constexpr Dimension frameWidth{422,Evidence::OfficialSpecification}; // Annotated official image.
    static constexpr Dimension frameDepth{414,Evidence::OfficialSpecification};
    static constexpr Dimension plateHeight{350,Evidence::PhotoEstimate}; // Image height convention is ambiguous.
    static constexpr Dimension plateThickness{10,Evidence::PhotoEstimate};
    static constexpr Dimension sideDepth{64,Evidence::PhotoEstimate};
    static constexpr Dimension tableWidth{320,Evidence::PhotoEstimate},tableDepth{200,Evidence::PhotoEstimate};
    static constexpr Dimension tableThickness{8,Evidence::PhotoEstimate};
    static constexpr Dimension tableTop{70,Evidence::PhotoEstimate};
    static constexpr Dimension holePitch{50,Evidence::PhotoEstimate},holeDiameter{6,Evidence::PhotoEstimate};
    static constexpr Dimension xRailWidth{15,Evidence::OfficialSpecification}; // HGH15 designation, width nominal.
    static constexpr Dimension guideSeparation{100,Evidence::PhotoEstimate};
    static constexpr Dimension gantryZ{-119,Evidence::PhotoEstimate};
    static constexpr Dimension topRailY{338,Evidence::PhotoEstimate};
    static constexpr Dimension bottomRailY{238,Evidence::PhotoEstimate};
    static constexpr Dimension zGuideLength{200,Evidence::PhotoEstimate};
    static constexpr Dimension zGuideBottom{140,Evidence::PhotoEstimate};
    static constexpr Dimension zGuideSpacing{44,Evidence::PhotoEstimate};
    static constexpr Dimension spindleBodyLength{112,Evidence::PhotoEstimate};
    static constexpr Dimension clampHeight{70,Evidence::PhotoEstimate};
    static constexpr Dimension spindleProtrusion{25,Evidence::OfficialSpecification}; // Assembly guide p13.
    static constexpr Dimension screwDiameter{8,Evidence::PhotoEstimate};
    static constexpr Dimension screwPitch{2,Evidence::PhotoEstimate};
    static constexpr Dimension powerWidth{210,Evidence::PhotoEstimate},powerHeight{110,Evidence::PhotoEstimate},powerDepth{48,Evidence::PhotoEstimate};
    static constexpr double xCenter=travel[0]/2,yCenter=travel[1]/2,cutPlaneZ=0;
    static constexpr double workpieceHeight=14,workpieceTop=tableTop.mm+workpieceHeight;
    // Tool datum is adjustable setup geometry, not a firmware coordinate specification.
    static constexpr double penetrationReserve=10,zDatum=workpieceTop+travel[2]-penetrationReserve;
    static constexpr double surfaceMachineZ=workpieceTop-zDatum;
    static constexpr double safeWorkZ=5;
    static constexpr std::array<float,3> gold{0.94f,0.57f,0.13f},silver{0.72f,0.76f,0.81f},black{0.065f,0.075f,0.085f};
};
}
