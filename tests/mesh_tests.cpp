#include "ui/mesh.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace cnc::mesh;
void require(bool yes,const char* why) { if(!yes) throw std::runtime_error(why); }
QVector3D point(const Vertex& v) { return {v.x,v.y,v.z}; }
void healthy(const Mesh& m) {
    require(!m.empty() && m.size()%3==0,"triangle mesh shape");
    for(const auto& v:m) {
        for(float x:{v.x,v.y,v.z,v.nx,v.ny,v.nz}) require(std::isfinite(x),"finite vertex/normal");
        require(std::abs(QVector3D{v.nx,v.ny,v.nz}.length()-1)<1e-4f,"unit surface normals");
    }
    for(std::size_t i=0;i<m.size();i+=3) require(QVector3D::crossProduct(point(m[i+1])-point(m[i]),point(m[i+2])-point(m[i])).lengthSquared()>1e-12f,"nondegenerate triangle");
}
double topArea(const Mesh& m) {
    double area=0;
    for(std::size_t i=0;i<m.size();i+=3) if(m[i].ny>.99f) area+=QVector3D::crossProduct(point(m[i+1])-point(m[i]),point(m[i+2])-point(m[i])).length()/2;
    return area;
}
float cross(QVector2D a,QVector2D b,QVector2D c) { auto u=b-a,v=c-a; return u.x()*v.y()-u.y()*v.x(); }
bool inTriangle(QVector2D p,QVector2D a,QVector2D b,QVector2D c) {
    float x=cross(a,b,p),y=cross(b,c,p),z=cross(c,a,p);
    return (x>=-1e-4f && y>=-1e-4f && z>=-1e-4f) || (x<=1e-4f && y<=1e-4f && z<=1e-4f);
}
bool tableCovers(const Mesh& m,QVector2D p) {
    for(std::size_t i=0;i<m.size();i+=3) if(m[i].ny>.99f && inTriangle(p,{m[i].x,m[i].z},{m[i+1].x,m[i+1].z},{m[i+2].x,m[i+2].z})) return true;
    return false;
}
}
int main() {
    try {
        healthy(box()); healthy(cylinder(48,.58f)); healthy(tSlot(40)); healthy(tSlot(20));
        healthy(prism(roundedRectangle(19,4,2),56)); // Capsule: coincident arc endpoints.
        healthy(leadScrew(394,2,3.8f));
        const auto table=perforatedTable(320,200,8,50,3); healthy(table);
        const double expected=320*200-24*3.141592653589793*9;
        require(std::abs(topArea(table)-expected)<3,"table has complete rectangle corners and open circular holes");
        for(int col=0;col<6;++col) for(int row=0;row<4;++row) {
            const QVector2D hole{-160+(col+.5f)*320/6,-100+(row+.5f)*50};
            require(!tableCovers(table,hole),"hole centre has no top-face triangle");
            require(tableCovers(table,hole+QVector2D{5,0}),"material remains outside hole");
        }
        require(tableCovers(table,{-159.9f,-99.9f}),"non-square cell corner is filled");
        const auto clamp=perforatedTable(78,78,70,1000,26); healthy(clamp);
        require(!tableCovers(clamp,{0,0}) && tableCovers(clamp,{30,0}),"spindle clamp has real bore");
        std::cout<<"PASS: mechanical mesh geometry\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n'; return 1; }
}
