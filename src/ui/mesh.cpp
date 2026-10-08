#include "ui/mesh.h"
#include <QQuaternion>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace cnc::mesh {
namespace {
constexpr float pi=3.14159265358979323846f;
void tri(Mesh& mesh,QVector3D a,QVector3D b,QVector3D c,QVector3D n) {
    if(QVector3D::crossProduct(b-a,c-a).lengthSquared()<1e-12f) return;
    for(auto p:{a,b,c}) mesh.push_back({p.x(),p.y(),p.z(),n.x(),n.y(),n.z()});
}
void quad(Mesh& mesh,QVector3D a,QVector3D b,QVector3D c,QVector3D d,QVector3D n) { tri(mesh,a,b,c,n); tri(mesh,a,c,d,n); }
float cross(QVector2D a,QVector2D b,QVector2D c) { const auto u=b-a,v=c-a; return u.x()*v.y()-u.y()*v.x(); }
bool inTriangle(QVector2D p,QVector2D a,QVector2D b,QVector2D c) { return cross(a,b,p)>=-1e-6f && cross(b,c,p)>=-1e-6f && cross(c,a,p)>=-1e-6f; }
}
Mesh box() {
    Mesh m;
    const QVector3D p[8]={{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,.5f},{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f}};
    const int f[6][4]={{0,3,2,1},{4,5,6,7},{0,4,7,3},{1,2,6,5},{0,1,5,4},{3,7,6,2}};
    const QVector3D n[6]={{0,0,-1},{0,0,1},{-1,0,0},{1,0,0},{0,-1,0},{0,1,0}};
    for(int i=0;i<6;++i) quad(m,p[f[i][0]],p[f[i][1]],p[f[i][2]],p[f[i][3]],n[i]);
    return m;
}
Mesh cylinder(int sides,float inner) {
    Mesh m;
    for(int i=0;i<sides;++i) {
        const float a=i*2*pi/sides,b=(i+1)*2*pi/sides;
        const QVector3D p{std::cos(a),-.5f,std::sin(a)},q{std::cos(b),-.5f,std::sin(b)},r{q.x(),.5f,q.z()},s{p.x(),.5f,p.z()};
        auto n=QVector3D{std::cos((a+b)/2),0,std::sin((a+b)/2)};
        quad(m,p,q,r,s,n);
        if(inner>0) {
            const QVector3D ip{p.x()*inner,-.5f,p.z()*inner},iq{q.x()*inner,-.5f,q.z()*inner},ir{iq.x(),.5f,iq.z()},is{ip.x(),.5f,ip.z()};
            quad(m,ip,is,ir,iq,-n);
            quad(m,s,r,ir,is,{0,1,0}); quad(m,p,ip,iq,q,{0,-1,0});
        } else { tri(m,{0,-.5f,0},q,p,{0,-1,0}); tri(m,{0,.5f,0},s,r,{0,1,0}); }
    }
    return m;
}
std::vector<QVector2D> roundedRectangle(float w,float h,float r) {
    std::vector<QVector2D> p;
    r=std::min(r,std::min(w,h)/2);
    const std::array<QVector2D,4> center={QVector2D{w/2-r,h/2-r},{-w/2+r,h/2-r},{-w/2+r,-h/2+r},{w/2-r,-h/2+r}};
    for(int i=0;i<4;++i) for(int j=0;j<=8;++j) { const float a=(i+j/8.f)*pi/2; p.push_back(center[i]+QVector2D{std::cos(a),std::sin(a)}*r); }
    return p;
}
Mesh prism(const std::vector<QVector2D>& input,float depth) {
    // Rounded profiles can meet at a zero-length straight edge (capsule shape).
    std::vector<QVector2D> contour;
    for(auto p:input) if(contour.empty() || (p-contour.back()).lengthSquared()>1e-8f) contour.push_back(p);
    if(contour.size()>1 && (contour.front()-contour.back()).lengthSquared()<1e-8f) contour.pop_back();
    if(contour.size()<3) throw std::invalid_argument("Prism requires a polygon");
    Mesh m;
    std::vector<std::size_t> indices(contour.size()); std::iota(indices.begin(),indices.end(),0);
    for(std::size_t i=0;i<contour.size();++i) {
        const auto a=contour[i],b=contour[(i+1)%contour.size()];
        const QVector3D n=QVector3D{b.y()-a.y(),a.x()-b.x(),0}.normalized();
        quad(m,{a.x(),a.y(),-depth/2},{b.x(),b.y(),-depth/2},{b.x(),b.y(),depth/2},{a.x(),a.y(),depth/2},n);
    }
    while(indices.size()>2) {
        bool clipped=false;
        for(std::size_t i=0;i<indices.size();++i) {
            const auto a=indices[(i+indices.size()-1)%indices.size()],b=indices[i],c=indices[(i+1)%indices.size()];
            if(cross(contour[a],contour[b],contour[c])<=1e-6f) continue;
            bool contains=false;
            for(auto p:indices) if(p!=a && p!=b && p!=c && inTriangle(contour[p],contour[a],contour[b],contour[c])) {contains=true;break;}
            if(contains) continue;
            const auto pa=contour[a],pb=contour[b],pc=contour[c];
            tri(m,{pa.x(),pa.y(),depth/2},{pb.x(),pb.y(),depth/2},{pc.x(),pc.y(),depth/2},{0,0,1});
            tri(m,{pc.x(),pc.y(),-depth/2},{pb.x(),pb.y(),-depth/2},{pa.x(),pa.y(),-depth/2},{0,0,-1});
            indices.erase(indices.begin()+static_cast<std::ptrdiff_t>(i)); clipped=true; break;
        }
        if(!clipped) throw std::runtime_error("Non-simple polygon in mechanical mesh");
    }
    return m;
}
Mesh tSlot(float width) {
    // Actual open T-shaped recesses in the outer cross section, not painted slot lines.
    const float s=width/40;
    const std::array<QVector2D,11> face={QVector2D{-20,-20},{-4,-20},{-4,-17},{-7,-17},{-7,-13},{-3,-9},{3,-9},{7,-13},{7,-17},{4,-17},{4,-20}};
    std::vector<QVector2D> contour;
    for(int side=0;side<4;++side) for(auto p:face) {
        for(int i=0;i<side;++i) p={-p.y(),p.x()};
        contour.push_back(p*s);
    }
    return prism(contour);
}
Mesh perforatedTable(float w,float d,float h,float pitch,float radius) {
    Mesh m;
    const int cols=std::max(1,static_cast<int>(w/pitch)),rows=std::max(1,static_cast<int>(d/pitch));
    const float cellW=w/cols,cellD=d/rows;
    std::vector<float> angles;
    for(int i=0;i<48;++i) angles.push_back(i*2*pi/48);
    // Include all rectangle corners so a non-square cell has no triangular corner gaps.
    const float corner=std::atan2(cellD,cellW);
    for(float a:{corner,pi-corner,pi+corner,2*pi-corner}) angles.push_back(a);
    std::sort(angles.begin(),angles.end());
    angles.erase(std::unique(angles.begin(),angles.end(),[](float a,float b){return std::abs(a-b)<1e-6f;}),angles.end());
    for(int col=0;col<cols;++col) for(int row=0;row<rows;++row) {
        const float cx=-w/2+(col+.5f)*cellW,cz=-d/2+(row+.5f)*cellD;
        for(std::size_t i=0;i<angles.size();++i) {
            const float a=angles[i],b=i+1<angles.size()?angles[i+1]:2*pi;
            auto point=[&](float angle,bool outer,float y) {
                const float x=std::cos(angle),z=std::sin(angle);
                const float r=outer?std::min(cellW/2/std::max(1e-7f,std::abs(x)),cellD/2/std::max(1e-7f,std::abs(z))):radius;
                return QVector3D{cx+x*r,y,cz+z*r};
            };
            const auto ia=point(a,false,h/2),ib=point(b,false,h/2),oa=point(a,true,h/2),ob=point(b,true,h/2);
            quad(m,ia,oa,ob,ib,{0,1,0});
            quad(m,point(a,false,-h/2),point(b,false,-h/2),point(b,true,-h/2),point(a,true,-h/2),{0,-1,0});
            quad(m,ia,ib,point(b,false,-h/2),point(a,false,-h/2),{-std::cos((a+b)/2),0,-std::sin((a+b)/2)});
        }
    }
    quad(m,{-w/2,-h/2,-d/2},{w/2,-h/2,-d/2},{w/2,h/2,-d/2},{-w/2,h/2,-d/2},{0,0,-1});
    quad(m,{-w/2,-h/2,d/2},{-w/2,h/2,d/2},{w/2,h/2,d/2},{w/2,-h/2,d/2},{0,0,1});
    quad(m,{-w/2,-h/2,-d/2},{-w/2,h/2,-d/2},{-w/2,h/2,d/2},{-w/2,-h/2,d/2},{-1,0,0});
    quad(m,{w/2,-h/2,-d/2},{w/2,-h/2,d/2},{w/2,h/2,d/2},{w/2,h/2,-d/2},{1,0,0});
    return m;
}
Mesh tube(const std::vector<QVector3D>& p,float radius,int sides) {
    Mesh m;
    for(std::size_t i=1;i<p.size();++i) {
        const auto direction=p[i]-p[i-1]; if(direction.lengthSquared()<1e-8f) continue;
        const auto q=QQuaternion::rotationTo({0,1,0},direction.normalized());
        for(int j=0;j<sides;++j) {
            const float a=j*2*pi/sides,b=(j+1)*2*pi/sides;
            const auto na=q.rotatedVector({std::cos(a),0,std::sin(a)}),nb=q.rotatedVector({std::cos(b),0,std::sin(b)});
            quad(m,p[i-1]+na*radius,p[i-1]+nb*radius,p[i]+nb*radius,p[i]+na*radius,(na+nb).normalized());
        }
    }
    return m;
}
Mesh leadScrew(float length,float pitch,float radius) {
    std::vector<QVector3D> p;
    const int steps=static_cast<int>(length/pitch*12);
    for(int i=0;i<=steps;++i) { const float f=i/static_cast<float>(steps),a=f*length/pitch*2*pi; p.push_back({std::cos(a)*radius,-length/2+f*length,std::sin(a)*radius}); }
    return tube(p,0.36f,5);
}
}
