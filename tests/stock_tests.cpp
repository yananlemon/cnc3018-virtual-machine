#include "core/stock.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <algorithm>
#include <map>
#include <array>
namespace {
void require(bool value,const char* what) {if(!value) throw std::runtime_error(what);}
void near(double a,double b,double tolerance,const char* what) {require(std::abs(a-b)<tolerance,what);}
cnc::StockSetup setup() {cnc::StockSetup s;s.x=100;s.y=50;s.width=20;s.depth=20;s.thickness=10;s.toolDatum=150;s.resolution=.1;s.cutter.diameter=4;return s;}
cnc::MotionStep cut(double x,double y,double z,double bx,double by,double bz,int kind=1,double spindle=12000) {return {{x,y,z-80},{bx,by,bz-80},kind,spindle};}
}
int main() {
try {
    auto s=setup();cnc::Stock flat(s);
    const auto pass=cut(104,60,8,116,60,8);
    flat.observe(pass);flat.flush();near(flat.heightAt(110,60),8,1e-5,"flat center depth");near(flat.heightAt(110,61.9),8,1e-5,"flat footprint");near(flat.heightAt(110,62.2),10,1e-5,"outside cutter");
    const auto first=flat.heights();const auto volume=flat.stats().removedVolume;flat.observe(pass);flat.flush();require(flat.heights()==first,"repeat pass must be idempotent");near(flat.stats().removedVolume,volume,1e-5,"repeat volume");
    flat.observe(cut(104,60,7,116,60,7));flat.flush();near(flat.heightAt(110,60),7,1e-5,"deeper pass");
    flat.observe(cut(104,64,6,116,64,6,0));flat.observe(cut(104,66,6,116,66,6,1,0));flat.flush();near(flat.heightAt(110,64),10,1e-5,"rapid does not cut");near(flat.heightAt(110,66),10,1e-5,"spindle off does not cut");require(flat.stats().rapidContacts && flat.stats().stoppedContacts,"contact diagnostics");
    s.cutter.shape=cnc::CutterShape::Ball;cnc::Stock ball(s);ball.observe(pass);ball.flush();near(ball.heightAt(110,61),8+2-std::sqrt(3.0),1e-5,"ball analytic cross section");
    s.cutter.shape=cnc::CutterShape::VBit;s.cutter.tipDiameter=0;s.cutter.angle=60;cnc::Stock v(s);v.observe(pass);v.flush();near(v.heightAt(110,60.5),8+.5/std::tan(3.141592653589793/6),1e-5,"V analytic cross section");
    for(auto shape:{cnc::CutterShape::Flat,cnc::CutterShape::Ball,cnc::CutterShape::VBit}) {
        s.cutter.shape=shape;cnc::Stock whole(s),parts(s);
        const auto ramp=cut(104,60,6,116,61,8);whole.observe(ramp);whole.flush();
        for(int i=0;i<240;++i) {auto step=ramp;for(int a=0;a<3;++a) {step.from[a]=ramp.from[a]+(ramp.to[a]-ramp.from[a])*i/240;step.to[a]=ramp.from[a]+(ramp.to[a]-ramp.from[a])*(i+1)/240;}parts.observe(step);if(i%11==0) parts.flush();}parts.flush();
        double error=0;for(std::size_t i=0;i<whole.heights().size();++i) error=std::max(error,std::abs(double(whole.heights()[i]-parts.heights()[i])));require(error<2e-5,"swept envelope independent of step/frame partition");
        for(int t=0;t<whole.tilesX()*whole.tilesY();++t) {const auto mesh=whole.meshTile(t);require(mesh.indices.size()%3==0,"mesh triangles");for(const auto& vertex:mesh.vertices) require(std::isfinite(vertex.y)&&std::isfinite(vertex.nx),"finite surface/normals");for(auto index:mesh.indices) require(index<mesh.vertices.size(),"valid mesh index");}
    }

    // Weld geometric coordinates across tiles; every exported edge must have two
    // opposite faces, including a through-cut with a zero-height opening.
    for(bool through:{false,true}) {
        auto closedSetup=setup();closedSetup.width=120;closedSetup.depth=12;closedSetup.resolution=.5;cnc::Stock closed(closedSetup);
        if(through) {closed.observe(cut(115,56,-1,205,56,-1));closed.flush();}
        using Key=std::array<long long,3>;using Edge=std::pair<Key,Key>;
        std::map<Edge,std::pair<int,int>> edges;
        auto key=[](const cnc::StockVertex& v){return Key{std::llround(v.x*100000),std::llround(v.y*100000),std::llround(v.z*100000)};};
        for(int t=0;t<closed.tilesX()*closed.tilesY();++t) {
            const auto mesh=closed.meshTile(t);
            for(std::size_t i=0;i<mesh.indices.size();i+=3) for(int j=0;j<3;++j) {
                auto a=key(mesh.vertices[mesh.indices[i+j]]),b=key(mesh.vertices[mesh.indices[i+(j+1)%3]]);
                require(a!=b,"nondegenerate exported edges");const bool forward=a<b;if(!forward) std::swap(a,b);auto& edge=edges[{a,b}];++edge.first;edge.second+=forward?1:-1;
            }
        }
        for(const auto& [key,edge]:edges) {static_cast<void>(key);require(edge.first==2 && edge.second==0,"closed manifold STL across tiles/through-cut");}
    }
    // Tool changes affect the next cut without replacing material.
    const auto vHeights=v.heights();v.changeCutter(setup().cutter);require(v.heights()==vHeights,"tool change preserves stock");
    cnc::Controller controller;cnc::Stock executed(setup());controller.setMotionObserver([&](const cnc::MotionStep& step){executed.observe(step);});controller.ingest("$X\n");controller.takeOutput();controller.ingest("G53G0X104Y60Z-69\n");controller.tick(30);controller.takeOutput();controller.ingest("M3S12000\nG1Z-72F300\nG1X116F600\n");controller.tick(.2);controller.ingest("!");controller.tick(1);executed.flush();const auto held=executed.heights();controller.tick(1);executed.flush();require(held==executed.heights(),"pause preserves stock");controller.ingest("~");controller.tick(30);executed.flush();near(executed.heightAt(110,60),8,1e-5,"controller execution cuts material");const auto done=executed.heights();controller.ingest(std::string(1,char(0x18)));executed.flush();require(done==executed.heights(),"reset preserves material");
    controller.ingest("$X\n");controller.ingest("G92X0Y0Z0\n");require(done==executed.heights(),"G92 never relocates stock");executed.restore();near(executed.heightAt(110,60),10,1e-5,"explicit restore");

    cnc::Stock arcStock(setup());cnc::Controller arcController;std::uint64_t arcSteps=0;
    arcController.setMotionObserver([&](const cnc::MotionStep& step){if(step.kind==2) ++arcSteps;arcStock.observe(step);});
    arcController.ingest("$X\nG53G0X106Y60Z-69\n");arcController.tick(40);arcController.takeOutput();
    arcController.ingest("M3S12000\nG1Z-72F300\nG2X114Y60I4J0F400\n");arcController.tick(40);arcStock.flush();
    require(arcSteps>100,"arc observed during execution");near(arcStock.heightAt(110,64),8,.002,"arc cutting footprint");near(arcStock.heightAt(110,56),10,.002,"arc does not cut unused half circle");
    std::cout<<"material removal contract passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
