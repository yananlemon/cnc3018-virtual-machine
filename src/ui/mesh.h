#pragma once
#include <QVector2D>
#include <QVector3D>
#include <vector>

namespace cnc::mesh {
struct Vertex { float x,y,z,nx,ny,nz; };
using Mesh=std::vector<Vertex>;
Mesh box();
Mesh cylinder(int sides=48,float inner=0);
Mesh prism(const std::vector<QVector2D>& contour,float depth=1);
std::vector<QVector2D> roundedRectangle(float width,float height,float radius);
Mesh tSlot(float width);
Mesh perforatedTable(float width,float depth,float thickness,float pitch,float radius);
Mesh tube(const std::vector<QVector3D>& points,float radius,int sides=8);
Mesh leadScrew(float length,float pitch,float radius);
}
