#pragma once
#include "core/controller.h"
#include <optional>
#include <vector>

namespace cnc {
enum class CutterShape { Flat, Ball, VBit };
struct Cutter {
    CutterShape shape=CutterShape::Flat;
    double diameter=3.175,angle=60,tipDiameter=.1,cuttingLength=12,stickout=22;
};
struct StockSetup {
    double x=85,y=47.5,width=130,depth=85,thickness=14;
    double bottom=70,toolDatum=154,resolution=.15;
    double workX=120,workY=65; // Import placement only; subsequent G92 never moves stock.
    Cutter cutter{};
};
struct StockStats {
    double removedVolume=0,maxDepth=0;
    std::uint64_t changes=0,segments=0,rapidContacts=0,stoppedContacts=0,shankContacts=0,tableContacts=0;
};
struct StockVertex { float x,y,z,nx,ny,nz; };
struct StockMesh { std::vector<StockVertex> vertices; std::vector<std::uint32_t> indices; };
class Stock {
public:
    static constexpr int tileSize=96;
    explicit Stock(StockSetup setup={});
    void configure(StockSetup setup);
    void changeCutter(Cutter cutter);
    void restore();
    void setPlacement(double x,double y) { setup_.workX=x;setup_.workY=y; }
    void observe(const MotionStep& step);
    void flush();
    const StockSetup& setup() const { return setup_; }
    const StockStats& stats() const { return stats_; }
    std::uint64_t revision() const { return revision_; }
    int columns() const { return columns_; }
    int rows() const { return rows_; }
    int tilesX() const { return (columns_-1+tileSize-1)/tileSize; }
    int tilesY() const { return (rows_-1+tileSize-1)/tileSize; }
    const std::vector<float>& heights() const { return heights_; }
    double heightAt(double x,double y) const;
    std::vector<int> takeDirtyTiles();
    StockMesh meshTile(int tile) const;
    void exportStl(const std::string& path) const;
private:
    StockSetup setup_;
    StockStats stats_;
    int columns_=0,rows_=0;
    double dx_=0,dy_=0;
    std::vector<float> heights_;
    std::vector<bool> dirty_;
    std::optional<MotionStep> pending_;
    std::uint64_t revision_=0;
    void sweep(const MotionStep& step);
    void mark(int x,int y);
    float height(int x,int y) const;
};
}
