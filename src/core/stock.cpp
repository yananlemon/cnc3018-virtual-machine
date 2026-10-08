#include "core/stock.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <limits>
#include <stdexcept>

namespace cnc {
namespace {
constexpr double pi=3.14159265358979323846;
void validateCutter(const Cutter& t) {
    for(double v:{t.diameter,t.angle,t.tipDiameter,t.cuttingLength,t.stickout}) if(!std::isfinite(v)) throw std::invalid_argument("Non-finite cutter");
    if(t.diameter<=0 || t.diameter>30 || t.angle<5 || t.angle>170 || t.tipDiameter<0 ||
       t.tipDiameter>t.diameter || t.cuttingLength<=0 || t.cuttingLength>t.stickout || t.stickout>40) throw std::invalid_argument("Invalid cutter dimensions");
}
double norm(const Position& p) { return std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]); }
Position delta(const Position& a,const Position& b) { return {b[0]-a[0],b[1]-a[1],b[2]-a[2]}; }
// Minimum cutting surface over an entire straight sweep, not discrete circular stamps.
double envelope(const Cutter& tool,double ax,double ay,double az,double bx,double by,double bz,double px,double py) {
    const double radius=tool.diameter/2, vx=bx-ax,vy=by-ay,length=std::hypot(vx,vy);
    auto profile=[&](double d) {
        if(tool.shape==CutterShape::Flat) return 0.0;
        if(tool.shape==CutterShape::Ball) return radius-std::sqrt(std::max(0.0,radius*radius-d*d));
        return std::max(0.0,d-tool.tipDiameter/2)/std::tan(tool.angle*pi/360);
    };
    if(length<1e-10) {
        const double d=std::hypot(px-ax,py-ay);
        return d<=radius+1e-10 ? std::min(az,bz)+profile(std::min(radius,d)) : std::numeric_limits<double>::infinity();
    }
    const double ux=vx/length,uy=vy/length,s=(px-ax)*ux+(py-ay)*uy;
    const double perpendicular=(px-ax)*uy-(py-ay)*ux,p2=perpendicular*perpendicular;
    if(p2>radius*radius+1e-10) return std::numeric_limits<double>::infinity();
    const double reach=std::sqrt(std::max(0.0,radius*radius-p2));
    const double lo=std::max(0.0,s-reach),hi=std::min(length,s+reach);
    if(lo>hi+1e-10) return std::numeric_limits<double>::infinity();
    const double slope=(bz-az)/length;
    if(tool.shape==CutterShape::Flat) return az+slope*(slope>=0?lo:hi);
    auto value=[&](double u) { u=std::clamp(u,lo,hi); return az+slope*u+profile(std::min(radius,std::sqrt(p2+(u-s)*(u-s)))); };
    double result=std::min(value(lo),value(hi));
    if(tool.shape==CutterShape::Ball) {
        result=std::min(result,value(s-slope*std::sqrt(std::max(0.0,radius*radius-p2)/(1+slope*slope))));
    } else {
        const double q=1/std::tan(tool.angle*pi/360);
        if(std::abs(slope)<q) result=std::min(result,value(s-slope*std::sqrt(p2/(q*q-slope*slope))));
        result=std::min(result,value(s));
        const double tip=tool.tipDiameter/2;
        if(p2<=tip*tip) {
            const double flat=std::sqrt(std::max(0.0,tip*tip-p2));
            result=std::min({result,value(s-flat),value(s+flat)});
        }
    }
    return result;
}
}
Stock::Stock(StockSetup setup) { configure(setup); }
void Stock::configure(StockSetup setup) {
    for(double v:{setup.x,setup.y,setup.width,setup.depth,setup.thickness,setup.bottom,setup.toolDatum,setup.resolution,setup.workX,setup.workY})
        if(!std::isfinite(v)) throw std::invalid_argument("Non-finite stock");
    if(setup.width<=0 || setup.depth<=0 || setup.x<0 || setup.y<0 || setup.x+setup.width>300.0001 ||
       setup.y+setup.depth>180.0001 || setup.thickness<1 || setup.thickness>60 || setup.resolution<.025 ||
       setup.resolution>2 || setup.toolDatum<100 || setup.toolDatum>220 || std::abs(setup.workX)>1000 || std::abs(setup.workY)>1000)
        throw std::invalid_argument("Stock must fit X0..300 / Y0..180, valid thickness and resolution");
    validateCutter(setup.cutter);
    while((std::ceil(setup.width/setup.resolution)+1)*(std::ceil(setup.depth/setup.resolution)+1)>2000000)
        setup.resolution*=1.02;
    setup_=setup; columns_=static_cast<int>(std::ceil(setup.width/setup.resolution))+1; rows_=static_cast<int>(std::ceil(setup.depth/setup.resolution))+1;
    dx_=setup.width/(columns_-1); dy_=setup.depth/(rows_-1); restore();
}
void Stock::changeCutter(Cutter cutter) { validateCutter(cutter); flush(); setup_.cutter=cutter; ++revision_; std::fill(dirty_.begin(),dirty_.end(),true); }
void Stock::restore() {
    pending_.reset(); heights_.assign(static_cast<std::size_t>(columns_)*rows_,static_cast<float>(setup_.thickness));
    dirty_.assign(static_cast<std::size_t>(tilesX())*tilesY(),true); stats_={}; ++revision_;
}
void Stock::observe(const MotionStep& step) {
    const auto move=delta(step.from,step.to);
    if(norm(move)<1e-12) return;
    if(pending_) {
        const auto previous=delta(pending_->from,pending_->to);
        const double a=norm(previous),b=norm(move),dot=previous[0]*move[0]+previous[1]*move[1]+previous[2]*move[2];
        // Collinear only: arcs and changing Z slopes never get silently replaced by chords.
        if(step.kind!=2 && step.kind!=3 && step.kind==pending_->kind && step.spindle==pending_->spindle &&
           norm(delta(pending_->to,step.from))<1e-7 && a+b<=2 && dot>=a*b*(1-1e-12)) {
            pending_->to=step.to; return;
        }
        flush();
    }
    pending_=step;
}
void Stock::flush() { if(pending_) { const auto step=*pending_; pending_.reset(); sweep(step); } }
void Stock::mark(int x,int y) {
    for(int ty=std::max(0,y-1)/tileSize;ty<=std::min(rows_-2,y+1)/tileSize;++ty)
        for(int tx=std::max(0,x-1)/tileSize;tx<=std::min(columns_-2,x+1)/tileSize;++tx) dirty_[static_cast<std::size_t>(ty)*tilesX()+tx]=true;
}
void Stock::sweep(const MotionStep& step) {
    ++stats_.segments;
    const double r=setup_.cutter.diameter/2;
    const int xmin=std::max(0,static_cast<int>(std::floor((std::min(step.from[0],step.to[0])-r-setup_.x)/dx_)));
    const int xmax=std::min(columns_-1,static_cast<int>(std::ceil((std::max(step.from[0],step.to[0])+r-setup_.x)/dx_)));
    const int ymin=std::max(0,static_cast<int>(std::floor((std::min(step.from[1],step.to[1])-r-setup_.y)/dy_)));
    const int ymax=std::min(rows_-1,static_cast<int>(std::ceil((std::max(step.from[1],step.to[1])+r-setup_.y)/dy_)));
    const double az=step.from[2]+setup_.toolDatum-setup_.bottom,bz=step.to[2]+setup_.toolDatum-setup_.bottom;
    if(std::min(az,bz)>setup_.thickness+1e-7 || xmin>xmax || ymin>ymax) return;
    const bool cutting=step.kind>=1 && step.kind<=3 && step.spindle>0;
    bool rapid=false,stopped=false,shank=false,table=false;
    const double length=std::hypot(step.to[0]-step.from[0],step.to[1]-step.from[1]);
    const double vx=length>0?(step.to[0]-step.from[0])/length:0,vy=length>0?(step.to[1]-step.from[1])/length:0;
    for(int y=ymin;y<=ymax;++y) for(int x=xmin;x<=xmax;++x) {
        const double px=setup_.x+x*dx_,py=setup_.y+y*dy_;
        const double lower=envelope(setup_.cutter,step.from[0],step.from[1],az,step.to[0],step.to[1],bz,px,py);
        if(!std::isfinite(lower)) continue;
        const std::size_t index=static_cast<std::size_t>(y)*columns_+x;
        const double current=heights_[index];
        if(lower<-1e-6) table=true;
        const double u=length>1e-10?std::clamp(((px-step.from[0])*vx+(py-step.from[1])*vy)/length,0.0,1.0):0;
        if(current>az+(bz-az)*u+setup_.cutter.cuttingLength+1e-5) shank=true;
        if(current<=lower+1e-6 || current<=0) continue;
        if(!cutting) { if(step.kind==0 || step.kind>=4) rapid=true; else stopped=true; continue; }
        const float h=static_cast<float>(std::clamp(lower,0.0,setup_.thickness));
        if(h<heights_[index]-1e-6f) {
            const double weight=(x==0 || x==columns_-1?.5:1)*(y==0 || y==rows_-1?.5:1);
            stats_.removedVolume+=(heights_[index]-h)*dx_*dy_*weight;
            stats_.maxDepth=std::max(stats_.maxDepth,setup_.thickness-h); ++stats_.changes;
            heights_[index]=h; mark(x,y);
        }
    }
    stats_.rapidContacts+=rapid?1:0; stats_.stoppedContacts+=stopped?1:0;
    stats_.shankContacts+=shank?1:0; stats_.tableContacts+=table?1:0;
}
float Stock::height(int x,int y) const { return heights_[static_cast<std::size_t>(std::clamp(y,0,rows_-1))*columns_+std::clamp(x,0,columns_-1)]; }
double Stock::heightAt(double x,double y) const {
    if(x<setup_.x || y<setup_.y || x>setup_.x+setup_.width || y>setup_.y+setup_.depth) return 0;
    const double gx=(x-setup_.x)/dx_,gy=(y-setup_.y)/dy_;
    const int ix=std::min(columns_-2,static_cast<int>(gx)),iy=std::min(rows_-2,static_cast<int>(gy));
    const double a=gx-ix,b=gy-iy;
    return (1-a)*(1-b)*height(ix,iy)+a*(1-b)*height(ix+1,iy)+(1-a)*b*height(ix,iy+1)+a*b*height(ix+1,iy+1);
}
std::vector<int> Stock::takeDirtyTiles() {
    std::vector<int> result; for(std::size_t i=0;i<dirty_.size();++i) if(dirty_[i]) { result.push_back(static_cast<int>(i)); dirty_[i]=false; } return result;
}
StockMesh Stock::meshTile(int tile) const {
    StockMesh m;
    const int x0=(tile%tilesX())*tileSize,y0=(tile/tilesX())*tileSize;
    const int x1=std::min(columns_-1,x0+tileSize),y1=std::min(rows_-1,y0+tileSize),w=x1-x0+1,h=y1-y0+1;
    const int count=w*h; m.vertices.reserve(static_cast<std::size_t>(count)*2+tileSize*8);
    for(int layer=0;layer<2;++layer) for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
        const double nx=-(height(x+1,y)-height(x-1,y))/(dx_*(x==0 || x==columns_-1?1:2));
        const double nz=-(height(x,y+1)-height(x,y-1))/(dy_*(y==0 || y==rows_-1?1:2)),n=std::sqrt(nx*nx+1+nz*nz);
        m.vertices.push_back({static_cast<float>(setup_.x+x*dx_-150),static_cast<float>(setup_.bottom+(layer==0?height(x,y):0)),
            static_cast<float>(setup_.y+y*dy_-90),static_cast<float>(layer==0?nx/n:0),layer==0?static_cast<float>(1/n):-1.f,static_cast<float>(layer==0?nz/n:0)});
    }
    auto triangle=[&](int a,int b,int c) {
        const auto &p=m.vertices[static_cast<std::size_t>(a)],&q=m.vertices[static_cast<std::size_t>(b)],&r=m.vertices[static_cast<std::size_t>(c)];
        const double ux=q.x-p.x,uy=q.y-p.y,uz=q.z-p.z,vx=r.x-p.x,vy=r.y-p.y,vz=r.z-p.z;
        if(std::pow(uy*vz-uz*vy,2)+std::pow(uz*vx-ux*vz,2)+std::pow(ux*vy-uy*vx,2)>1e-14)
            for(int i:{a,b,c}) m.indices.push_back(static_cast<std::uint32_t>(i));
    };
    for(int y=0;y<h-1;++y) for(int x=0;x<w-1;++x) {
        const int a=y*w+x,b=a+1,c=a+w+1,d=a+w;
        if(height(x0+x,y0+y)<=0 && height(x0+x+1,y0+y)<=0 && height(x0+x,y0+y+1)<=0 && height(x0+x+1,y0+y+1)<=0) continue;

        if(height(x0+x,y0+y)>0 || height(x0+x+1,y0+y+1)>0 || height(x0+x+1,y0+y)>0) {triangle(a,c,b);triangle(a+count,b+count,c+count);}
        if(height(x0+x,y0+y)>0 || height(x0+x,y0+y+1)>0 || height(x0+x+1,y0+y+1)>0) {triangle(a,d,c);triangle(a+count,c+count,d+count);}
    }
    auto side=[&](int a,int b,float nx,float nz) {
        const int begin=static_cast<int>(m.vertices.size());
        for(int i:{a,b,b+count,a+count}) { auto v=m.vertices[static_cast<std::size_t>(i)]; v.nx=nx;v.ny=0;v.nz=nz; m.vertices.push_back(v); }
        triangle(begin,begin+1,begin+2); triangle(begin,begin+2,begin+3);
    };
    if(y0==0) for(int x=0;x<w-1;++x) side(x,x+1,0,-1);
    if(y1==rows_-1) for(int x=0;x<w-1;++x) side((h-1)*w+x+1,(h-1)*w+x,0,1);
    if(x0==0) for(int y=0;y<h-1;++y) side((y+1)*w,y*w,-1,0);
    if(x1==columns_-1) for(int y=0;y<h-1;++y) side(y*w+w-1,(y+1)*w+w-1,1,0);
    return m;
}
void Stock::exportStl(const std::string& path) const {
    std::ofstream file(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(path.data()),path.size())),std::ios::binary); if(!file) throw std::runtime_error("Cannot open STL output");
    char header[80]{}; const std::string title="CNC3018 VM executed material surface"; std::copy(title.begin(),title.end(),header); file.write(header,80);
    std::uint32_t count=0; file.write(reinterpret_cast<const char*>(&count),4);
    for(int tile=0;tile<tilesX()*tilesY();++tile) {
        const auto m=meshTile(tile);
        for(std::size_t i=0;i<m.indices.size();i+=3) {
            const auto &a=m.vertices[m.indices[i]],&b=m.vertices[m.indices[i+1]],&c=m.vertices[m.indices[i+2]];
            const Position u{b.x-a.x,b.y-a.y,b.z-a.z},v{c.x-a.x,c.y-a.y,c.z-a.z};
            Position n{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]}; const double length=norm(n);
            float normal[3]{static_cast<float>(n[0]/length),static_cast<float>(n[1]/length),static_cast<float>(n[2]/length)};
            file.write(reinterpret_cast<const char*>(normal),12);
            for(const auto* p:{&a,&b,&c}) { float xyz[3]{p->x,p->y,p->z}; file.write(reinterpret_cast<const char*>(xyz),12); }
            const std::uint16_t attribute=0; file.write(reinterpret_cast<const char*>(&attribute),2); ++count;
        }
    }
    file.seekp(80); file.write(reinterpret_cast<const char*>(&count),4);
    if(!file) throw std::runtime_error("STL write failed");
}
}
