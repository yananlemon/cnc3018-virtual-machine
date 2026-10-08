#include "ui/machine_view.h"
#include "core/kinematics.h"
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QQuaternion>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
using P=cnc::Lunyee3018ProUltra;
QVector3D rgb(const std::array<float,3>& c) { return {c[0],c[1],c[2]}; }
const auto gold=rgb(P::gold),silver=rgb(P::silver),black=rgb(P::black);
const QVector3D steel{.76f,.79f,.82f},darkSteel{.22f,.25f,.28f};
QMatrix4x4 axis(char a) { QMatrix4x4 t; if(a=='X') t.rotate(90,0,1,0); else if(a=='Y') t.rotate(-90,1,0,0); return t; }
}
MachineView::Node* MachineView::Node::child() { children.push_back(std::make_unique<Node>()); return children.back().get(); }
void MachineView::Node::box(QVector3D center,QVector3D size,QVector3D color) {
    QMatrix4x4 t; t.translate(center); t.scale(size); parts.push_back({t,color,0});
}
void MachineView::Node::rod(QVector3D center,float radius,float length,QVector3D color,char a) {
    QMatrix4x4 t; t.translate(center);
    if(a=='X') t.rotate(90,0,0,1); else if(a=='Z') t.rotate(90,1,0,0);
    t.scale(radius,length,radius); parts.push_back({t,color,1});
}
int MachineView::storeMesh(cnc::mesh::Mesh mesh) { meshes_.push_back(std::move(mesh)); return static_cast<int>(meshes_.size()-1); }
void MachineView::shaped(Node* node,int mesh,QVector3D center,QVector3D color,const QMatrix4x4& orientation,float metallic) {
    QMatrix4x4 t; t.translate(center); t*=orientation; node->parts.push_back({t,color,mesh,1,metallic});
}
MachineView::MachineView(cnc::Stock& stock,QWidget* parent):QOpenGLWidget(parent),stock_(stock) { setMinimumSize(560,450); setFocusPolicy(Qt::StrongFocus);setContextMenuPolicy(Qt::PreventContextMenu); buildScene(); }
MachineView::~MachineView() { cleanup(); }
void MachineView::cleanup() {
    if(!context() || !functionsReady_) return;
    makeCurrent(); destroyStockBuffers(); vao_.destroy(); buffer_.destroy(); program_.removeAllShaders(); ready_=false; functionsReady_=false; doneCurrent();
}
void MachineView::buildScene() {
    using namespace cnc::mesh;
    meshes_.clear(); root_=Node{}; coordinates_=Node{};
    storeMesh(box()); storeMesh(cylinder());
    const int washer=storeMesh(cylinder(48,.58f)),socket=storeMesh(cylinder(24,.43f)),hex=storeMesh(cylinder(6)),fanRing=storeMesh(cylinder(64,.84f));
    const int slot40=storeMesh(tSlot(40)),slot20=storeMesh(tSlot(20));
    const int motor=storeMesh(prism(roundedRectangle(42,42,3),48));
    const int plate=storeMesh(prism(roundedRectangle(static_cast<float>(P::sideDepth.mm),static_cast<float>(P::plateHeight.mm),14),static_cast<float>(P::plateThickness.mm)));
    const int rail=storeMesh(prism({{-7.5f,-6},{7.5f,-6},{7.5f,-2},{6,-1},{6,1},{7.5f,2},{7.5f,6},{-7.5f,6},{-7.5f,2},{-6,1},{-6,-1},{-7.5f,-2}}));
    auto rounded=[&](Node* n,QVector3D c,QVector3D s,QVector3D color,float r=2) {
        shaped(n,storeMesh(prism(roundedRectangle(s.x(),s.y(),r),s.z())),c,color);
    };
    auto extrusion=[&](Node* n,QVector3D c,float length,bool forty,char a) {
        auto t=axis(a); t.scale(1,1,length);
        if(forty) shaped(n,slot40,c,silver,t);
        else { shaped(n,slot20,c+QVector3D{0,-10,0},silver,t); shaped(n,slot20,c+QVector3D{0,10,0},silver,t); }
    };
    auto bolt=[&](Node* n,QVector3D c,char a,float radius=4) {
        QMatrix4x4 t; if(a=='X') t.rotate(90,0,0,1); else if(a=='Z') t.rotate(90,1,0,0);
        t.scale(radius,2,radius); shaped(n,washer,c,steel,t);
        t.scale(.76f,1.6f,.76f); shaped(n,socket,c,darkSteel,t);
    };
    auto screw=[&](Node* n,QVector3D c,float length,char a) {
        n->rod(c,3.5f,length,darkSteel,a);
        QMatrix4x4 t; if(a=='X') t.rotate(90,0,0,1); else if(a=='Z') t.rotate(90,1,0,0);
        shaped(n,storeMesh(leadScrew(length,static_cast<float>(P::screwPitch.mm),3.8f)),c,steel,t);
    };
    auto wheel=[&](Node* n,QVector3D c,char a) {
        n->rod(c,11,14,black,a);
        for(int i=0;i<36;++i) {
            const float angle=i*6.2831853f/36; QVector3D offset{std::cos(angle)*11,0,std::sin(angle)*11};
            if(a=='X') offset={0,offset.x(),offset.z()}; else if(a=='Z') offset={offset.x(),offset.z(),0};
            n->rod(c+offset,.35f,14,darkSteel,a);
        }
        QMatrix4x4 t; if(a=='X') t.rotate(90,0,0,1); else if(a=='Z') t.rotate(90,1,0,0);
        t.scale(11,1,11);
        shaped(n,hex,c+(a=='X'?QVector3D{-7.5f,0,0}:a=='Z'?QVector3D{0,0,7.5f}:QVector3D{0,7.5f,0}),black,t);
    };
    auto stepper=[&](Node* n,QVector3D c,char a) {
        auto t=axis(a); shaped(n,motor,c,black,t,.4f);
        auto cap=t; cap.scale(42,42,3);
        shaped(n,0,c+t.map(QVector3D{0,0,-23}),steel,cap);
        shaped(n,0,c+t.map(QVector3D{0,0,23}),darkSteel,cap);
        for(float x:{-15.5f,15.5f}) for(float y:{-15.5f,15.5f}) bolt(n,c+t.map(QVector3D{x,y,-26}),a,2.4f);
        for(int i=-18;i<=18;i+=3) {
            auto band=t; band.translate(0,0,static_cast<float>(i)); band.scale(42.2f,42.2f,.25f);
            shaped(n,0,c,{.11f,.12f,.13f},band,.5f);
        }
    };
    auto cable=[&](Node* n,std::vector<QVector3D> points,float radius=1.7f,QVector3D color=black) {
        shaped(n,storeMesh(tube(points,radius)),{},color,QMatrix4x4{},.05f);
    };
    // Fixture floor, then actual slotted 4040 side profiles and gold end plates.
    root_.box({0,-3,0},{730,6,610},{.095f,.12f,.15f}); root_.parts.back().metallic=0;
    const float hw=static_cast<float>(P::frameWidth.mm/2),hd=static_cast<float>(P::frameDepth.mm/2);
    for(float x:{-hw+25,hw-25}) {
        extrusion(&root_,{x,30,0},static_cast<float>(P::frameDepth.mm-20),true,'Z');
        for(float z:{-hd+30,hd-30}) root_.rod({x,7,z},13,14,black);
    }
    for(float z:{-hd+5,hd-5}) {
        rounded(&root_,{0,35,z},{static_cast<float>(P::frameWidth.mm),50,10},gold);
        for(float x:{-190.f,-172.f,172.f,190.f}) for(float y:{22.f,47.f}) bolt(&root_,{x,y,z+(z>0?6:-6)},'Z');
        for(float x:{-120.f,120.f}) bolt(&root_,{x,45,z+(z>0?6:-6)},'Z');
    }
    for(float x:{-110.f,110.f}) root_.rod({x,50,0},6,394,steel,'Z');
    screw(&root_,{0,45,0},394,'Z'); wheel(&root_,{0,45,hd+12},'Z');
    stepper(&root_,{0,45,-hd-24},'Z'); root_.rod({0,45,-hd+20},9,24,silver,'Z');
    for(float z:{-hd+26,hd-26}) {
        rounded(&root_,{145,57,z},{19,9,10},black,1);
        root_.box({138,61,z},{16,1,4},steel);
    }
    // Single machined table with actual through-holes, laser grid and ruler marks.
    table_=root_.child();
    shaped(table_,storeMesh(perforatedTable(static_cast<float>(P::tableWidth.mm),static_cast<float>(P::tableDepth.mm),static_cast<float>(P::tableThickness.mm),static_cast<float>(P::holePitch.mm),static_cast<float>(P::holeDiameter.mm/2))),{0,static_cast<float>(P::tableTop.mm-P::tableThickness.mm/2),0},silver);
    for(float x:{-110.f,110.f}) {
        rounded(table_,{x,52,0},{30,20,90},silver,3);
        for(float z:{-30.f,30.f}) { QMatrix4x4 t; t.rotate(90,1,0,0); t.scale(9,12,9); shaped(table_,washer,{x,50,z},darkSteel,t); }
    }
    const float top=static_cast<float>(P::tableTop.mm)+.06f;
    Mesh markingPlane;
    for(auto v:cnc::mesh::box()) if(v.ny>.99f) { v.y=0; markingPlane.push_back(v); }
    const int markingMesh=storeMesh(std::move(markingPlane));
    auto mark=[&](QVector3D c,QVector3D s,QVector3D color) {
        QMatrix4x4 scale; scale.scale(s.x(),1,s.z());
        shaped(table_,markingMesh,c,color,scale,0); table_->parts.back().decal=true;
    };
    for(int x=-150;x<=150;x+=10) mark({static_cast<float>(x),top,0},{x%50==0?.8f:.5f,0,180},{.40f,.43f,.46f});
    for(int z=-90;z<=90;z+=10) mark({0,top,static_cast<float>(z)},{300,0,z%50==0?.8f:.5f},{.40f,.43f,.46f});
    for(int x=-150;x<=150;x+=5) mark({static_cast<float>(x),top,95},{.25f,.06f,x%10==0?4.f:2.f},darkSteel);
    for(int z=-90;z<=90;z+=5) mark({155,top,static_cast<float>(z)},{z%10==0?4.f:2.f,.06f,.25f},darkSteel);
    // Table-local measurement overlay: actual 3D lines, arrows and simple stroke labels.
    const QVector3D xColor{.95f,.27f,.22f},yColor{.23f,.65f,1.f},originColor{.2f,1.f,.78f};
    const float mh=top+.8f;
    cable(&coordinates_,{{-150,mh,-90},{150,mh,-90},{150,mh,90},{-150,mh,90},{-150,mh,-90}},.3f,{.24f,.55f,.65f});
    cable(&coordinates_,{{-150,mh,-90},{-108,mh,-90}},.7f,xColor);
    cable(&coordinates_,{{-115,mh,-94},{-108,mh,-90},{-115,mh,-86}},.7f,xColor);
    cable(&coordinates_,{{-150,mh,-90},{-150,mh,-48}},.7f,yColor);
    cable(&coordinates_,{{-154,mh,-55},{-150,mh,-48},{-146,mh,-55}},.7f,yColor);
    cable(&coordinates_,{{-104,mh,-93},{-98,mh,-87}},.6f,xColor);
    cable(&coordinates_,{{-104,mh,-87},{-98,mh,-93}},.6f,xColor);
    cable(&coordinates_,{{-153,mh,-44},{-150,mh,-40},{-147,mh,-44}},.6f,yColor);
    cable(&coordinates_,{{-150,mh,-40},{-150,mh,-36}},.6f,yColor);
    for(float x:{-164.f,-155.f}) cable(&coordinates_,{{x,mh,-104},{x+5,mh,-104},{x+5,mh,-97},{x,mh,-97},{x,mh,-104}},.55f,originColor);
    cable(&coordinates_,{{-157,mh,-96},{-159,mh,-94}},.55f,originColor);
    cable(&coordinates_,{{-152,mh,-90},{-148,mh,-90}},.8f,originColor);
    cable(&coordinates_,{{-150,mh,-92},{-150,mh,-88}},.8f,originColor);
    for(float x:{-110.f,110.f}) for(float z:{-55.f,55.f}) bolt(table_,{x,top+1,z},'Y',3.5f);
    workpiece_=table_->child();

    const auto setup=stock_.setup();

    const auto fixtures=cnc::placedFixtures(fixtureSetup_,setup);
    for(const auto& clamp:fixtures.clamps) {
        if(!clamp.enabled) continue;
        auto fixture=table_->child();
        fixture->transform.translate(static_cast<float>(clamp.x-150),0,static_cast<float>(clamp.y-90));
        fixture->transform.rotate(static_cast<float>(-clamp.angle),0,1,0);
        const float h=static_cast<float>(clamp.followSurface?setup.thickness:clamp.contactHeight);
        const float base=static_cast<float>(setup.bottom),length=static_cast<float>(clamp.length),width=static_cast<float>(clamp.width);
        rounded(fixture,{-5,base+h/2,0},{8,h,width},steel);
        rounded(fixture,{length/2-8,base+h+2,0},{length,4,width},steel);
        fixture->rod({0,base+(h+22)/2,0},3,h+22,steel);
        wheel(fixture,{0,base+h+13,0},'Y');
    }
    auto g=root_.child();
    const float gz=static_cast<float>(P::gantryZ.mm),rt=static_cast<float>(P::topRailY.mm),rb=static_cast<float>(P::bottomRailY.mm),ly=(rt+rb)/2;
    for(float x:{-hw+5,hw-5}) {
        shaped(g,plate,{x,10+static_cast<float>(P::plateHeight.mm/2),gz},gold,axis('X'));
        for(float y:{23.f,40.f,57.f}) for(float z:{gz-22,gz+22}) bolt(g,{x+(x>0?6:-6),y,z},'X');
        for(float y:{rt,rb}) {
            bolt(g,{x+(x>0?6:-6),y,gz},'X');
            bolt(g,{x+(x>0?6:-6),y-15,gz+18},'X',3);
        }
    }
    for(float y:{rt,rb}) {
        extrusion(g,{0,y,gz},402,false,'X');
        auto t=axis('X'); t.scale(1,1,392); shaped(g,rail,{0,y,gz+18},steel,t);
        for(int x=-175;x<=175;x+=50) bolt(g,{static_cast<float>(x),y,gz+26},'Z',2.4f);
    }
    screw(g,{0,ly,gz+10},394,'X'); g->rod({183,ly,gz+10},10,25,silver,'X');
    for(float x:{178.f,185.f,192.f}) g->rod({x,ly,gz+10},10.15f,.9f,darkSteel,'X');
    stepper(g,{hw+24,ly,gz+10},'X'); wheel(g,{-hw-12,ly,gz+10},'X');
    for(float x:{-192.f,192.f}) {
        rounded(g,{x,rb+27,gz+25},{12,24,8},black,1);
        g->box({x+(x>0?-8:8),rb+31,gz+31},{13,1,4},steel);
    }
    g->rod({hw-5,186,gz+37},11,8,black,'Z');
    g->rod({hw-5,186,gz+46},14,12,{.78f,.025f,.018f},'Z');
    QMatrix4x4 e; e.rotate(90,1,0,0); e.scale(13,2,13); shaped(g,hex,{hw-5,186,gz+53},{.95f,.06f,.035f},e,.3f);

    // HGH15 bearing blocks, X-carriage, fixed Z rails/drive and movable gold 52mm clamp.
    xSlide_=g->child();
    for(float y:{rt,rb}) {
        rounded(xSlide_,{0,y,gz+26},{57,30,22},darkSteel);
        for(float x:{-20.f,20.f}) for(float dy:{-8.f,8.f}) bolt(xSlide_,{x,y+dy,gz+38},'Z',2.5f);
    }
    rounded(xSlide_,{0,242,gz+41},{80,210,10},gold,7);
    const float bottom=static_cast<float>(P::zGuideBottom.mm),length=static_cast<float>(P::zGuideLength.mm),upper=bottom+length;
    for(float x:{-static_cast<float>(P::zGuideSpacing.mm/2),static_cast<float>(P::zGuideSpacing.mm/2)}) {
        xSlide_->rod({x,bottom+length/2,-43},6,length,steel);
        // Vertical backing is an open-section extrusion, not a featureless slab.
        auto t=axis('Y'); t.scale(1,1,length); shaped(xSlide_,slot20,{x+(x<0?-10:10),bottom+length/2,-56},silver,t);
    }
    screw(xSlide_,{0,bottom+length/2,-43},length,'Y');
    for(float y:{bottom,upper}) {
        // Front edge -29 clears the rear of the spindle (-26) throughout Z travel.
        rounded(xSlide_,{0,y,-46},{84,10,34},gold,3);
        for(float x:{-30.f,30.f}) bolt(xSlide_,{x,y+6,-43},'Y',3);
        rounded(xSlide_,{-33,y+6,-25},{16,9,8},black,1);
        xSlide_->box({-33,y+12,-25},{12,1,5},steel);
    }
    stepper(xSlide_,{0,upper+30,-43},'Y'); wheel(xSlide_,{0,upper+63,-43},'Y');
    xSlide_->rod({0,upper-16,-43},9,20,silver);
    zSlide_=xSlide_->child();
    const int zBearing=storeMesh(cylinder(40,2.f/3));
    for(float x:{-22.f,22.f}) {
        QMatrix4x4 bearing; bearing.scale(9,54,9);
        shaped(zSlide_,zBearing,{x,115,-43},darkSteel,bearing);
    }
    shaped(zSlide_,storeMesh(perforatedTable(78,78,static_cast<float>(P::clampHeight.mm),1000,static_cast<float>(P::spindleDiameter/2))),{0,115,0},gold);
    rounded(zSlide_,{0,115,-34},{74,70,9},gold,3);
    for(float y:{93.f,137.f}) bolt(zSlide_,{30,y,41},'Z',3.3f);
    zSlide_->box({30,115,40},{1,68,.25f},darkSteel);
    spindle_=zSlide_->child();
    spindle_->rod({0,55+static_cast<float>(P::spindleBodyLength.mm/2),0},26,static_cast<float>(P::spindleBodyLength.mm),black);
    spindle_->rod({0,55,0},27,3,silver); spindle_->rod({0,168,0},27,3,silver);
    spindle_->rod({0,static_cast<float>((setup.cutter.stickout+14+55)/2),0},7.5f,static_cast<float>(55-setup.cutter.stickout-14),steel); spindle_->rod({0,30,0},8,1,darkSteel);
    for(int i=0;i<12;++i) spindle_->rod({0,23+i*.6f,0},7.8f,.18f,darkSteel);
    QMatrix4x4 nut; nut.scale(10,14,10); shaped(spindle_,hex,{0,static_cast<float>(setup.cutter.stickout+7),0},black,nut);

    // Render the same tool profile used by the material envelope.
    const auto tool=setup.cutter;
    Mesh cutter;
    auto radius=[&](double h) {
        const double r=tool.diameter/2;
        if(tool.shape==cnc::CutterShape::Ball && h<r) return std::sqrt(std::max(0.0,r*r-(r-h)*(r-h)));
        if(tool.shape==cnc::CutterShape::VBit) return std::min(r,tool.tipDiameter/2+h*std::tan(tool.angle*3.141592653589793/360));
        return r;
    };
    auto triangle=[&](QVector3D a,QVector3D b,QVector3D c) {
        auto n=QVector3D::crossProduct(b-a,c-a).normalized();
        if(n.isNull()) return;
        for(auto v:{a,b,c}) cutter.push_back({v.x(),v.y(),v.z(),n.x(),n.y(),n.z()});
    };
    for(int j=0;j<48;++j) for(int i=0;i<64;++i) {
        const double h0=tool.cuttingLength*j/48,h1=tool.cuttingLength*(j+1)/48;
        const double a=i*6.283185307179586/64,b=(i+1)*6.283185307179586/64;
        auto v=[&](double angle,double h) {return QVector3D{static_cast<float>(radius(h)*std::cos(angle)),static_cast<float>(h),static_cast<float>(radius(h)*std::sin(angle))};};
        triangle(v(a,h0),v(a,h1),v(b,h1));triangle(v(a,h0),v(b,h1),v(b,h0));
        if(j==0) triangle({0,0,0},v(a,0),v(b,0));
    }
    shaped(spindle_,storeMesh(std::move(cutter)),{},steel,QMatrix4x4{},.25f);
    spindle_->rod({0,static_cast<float>((tool.cuttingLength+tool.stickout)/2),0},static_cast<float>(tool.diameter/2),static_cast<float>(tool.stickout-tool.cuttingLength),steel);

    QMatrix4x4 fan; fan.scale(26,25,26); shaped(spindle_,fanRing,{0,184,0},black,fan,.35f);
    for(int i=0;i<8;++i) {
        QMatrix4x4 blade; blade.translate(0,189,0); blade.rotate(i*45.f,0,1,0); blade.scale(40,2,3);
        shaped(spindle_,0,{},black,blade,.25f);
    }
    spindle_->rod({0,189,0},5,8,steel);
    cable(spindle_,{{24,166,-3},{33,158,-8},{32,130,-20},{27,104,-33}},1.1f,{.65f,.035f,.025f});
    cable(spindle_,{{24,162,-5},{36,151,-11},{35,124,-22},{30,100,-36}},1.1f);
    // Rear control PCB and adjacent power enclosure, matching manual installation views.
    rounded(g,{111,ly,gz-36},{148,104,8},black,3);
    g->box({111,ly,gz-41},{136,91,2},{.07f,.19f,.39f});
    g->box({126,ly+8,gz-45},{41,32,6},black);
    for(int i=0;i<10;++i) g->box({107+i*4.f,ly+8,gz-50},{1,30,2},silver);
    for(float x:{55.f,168.f}) for(float y:{ly-38,ly+38}) bolt(g,{x,y,gz-45},'Z',2.5f);
    for(int i=0;i<7;++i) {
        g->box({64+i*16.f,ly+35,gz-44},{10,9,5},i%2?QVector3D{.2f,.48f,.59f}:QVector3D{.14f,.48f,.25f});
        g->box({64+i*16.f,ly-33,gz-44},{10,7,5},i%2?black:QVector3D{.60f,.04f,.03f});
    }
    for(int i=0;i<10;++i) g->rod({63+i*11.f,ly-6,gz-44},3,2,{.24f,.42f,.58f},'Z');
    const float pw=static_cast<float>(P::powerWidth.mm),ph=static_cast<float>(P::powerHeight.mm),pd=static_cast<float>(P::powerDepth.mm);
    rounded(g,{-84,ly-5,gz-pd/2-28},{pw,ph,pd},silver,4);
    g->box({21,ly-5,gz-pd/2-28},{3,ph,pd},black);
    g->box({24,ly-34,gz-pd/2-28},{5,20,25},black);
    g->box({24,ly+14,gz-pd/2-28},{5,15,14},{.53f,.025f,.015f});
    for(int i=0;i<6;++i) for(float y:{ly-30,ly+27}) rounded(g,{-15-i*6.f,y,gz-pd-29},{2.7f,23,1},black,1.3f);
    QMatrix4x4 grille; grille.rotate(90,1,0,0); grille.scale(24,1,24);
    shaped(g,fanRing,{-135,ly+8,gz-pd-29},black,grille,.1f);
    for(int i=0;i<8;++i) {
        const float a=i*.78539816f; g->rod({-135+std::cos(a)*18,ly+8+std::sin(a)*18,gz-pd-29},2.5f,1,black,'Z');
    }
    for(float x:{-157.f,-15.f}) {
        g->box({x,ly+54,gz-25},{18,6,50},silver); g->box({x,ly+40,gz-48},{18,28,6},silver);
        bolt(g,{x,ly+58,gz-20},'Y',3);
    }
    for(int x=-65;x<=65;x+=5) g->box({static_cast<float>(x),rb-24,gz-15},{2,24,20},darkSteel);
    cable(g,{{173,ly-25,gz-45},{192,220,-102},{203,137,-105},{205,70,-108},{0,35,-214}},2.5f);
    cable(g,{{190,ly+30,gz-42},{170,365,-92},{70,377,-92},{0,368,-85}},2.3f);
    cable(xSlide_,{{0,upper+48,-48},{35,upper+40,-62},{45,upper+20,-72},{35,upper-20,-75}},2.2f);
    cable(g,{{-170,ly-40,gz-55},{-211,180,-118},{-238,75,-155},{-250,20,-230}},3);
    for(int i=-7;i<=7;++i) {
        root_.box({static_cast<float>(i*45),.06f,0},{.3f,.05f,570},{.15f,.19f,.23f});
        root_.box({0,.06f,static_cast<float>(i*38)},{690,.05f,.3f},{.15f,.19f,.23f});
    }
}
void MachineView::buildGeometry() {
    geometry_.clear(); ranges_.clear();
    for(const auto& m:meshes_) {
        const int begin=static_cast<int>(geometry_.size()); geometry_.insert(geometry_.end(),m.begin(),m.end());
        ranges_.push_back({begin,static_cast<int>(m.size())});
    }
}
void MachineView::initializeGL() {
    if(!initializeOpenGLFunctions()) { renderer_="OpenGL 3.3 core functions unavailable"; return; }
    functionsReady_=true;
    connect(context(),&QOpenGLContext::aboutToBeDestroyed,this,&MachineView::cleanup,Qt::DirectConnection);
    renderer_=QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    const char* vertex=R"(#version 330 core
layout(location=0) in vec3 position; layout(location=1) in vec3 normal;
uniform mat4 mvp; uniform mat4 model; uniform mat3 normalMatrix;
out vec3 N; out vec3 P; out vec3 localP;
void main(){gl_Position=mvp*vec4(position,1); localP=position; N=normalMatrix*normal; P=(model*vec4(position,1)).xyz;}
)";
    const char* fragment=R"(#version 330 core
in vec3 N; in vec3 P; in vec3 localP; uniform bool woodMaterial; uniform float stockTop; uniform sampler2D heightMap; uniform vec2 stockOrigin; uniform vec2 stockSize; uniform float stockBottom; uniform vec3 color; uniform vec3 eye;
uniform float opacity; uniform float metallic; out vec4 frag;

float hash(vec2 p){return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);}
float noise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);}
float surface(vec2 p){vec2 uv=(p-stockOrigin)/stockSize;if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))return stockBottom;return stockBottom+texture(heightMap,(uv*(vec2(textureSize(heightMap,0))-1.0)+.5)/vec2(textureSize(heightMap,0))).r;}
void main(){vec3 n=normalize(N); vec3 v=normalize(eye-P);
vec3 l=normalize(vec3(-0.5,0.9,0.7)); vec3 l2=normalize(vec3(0.8,0.5,-0.6));
float d=max(dot(n,l),0.0); float d2=max(dot(n,l2),0.0);
float s=pow(max(dot(n,normalize(l+v)),0.0),mix(18.0,75.0,metallic));
float s2=pow(max(dot(n,normalize(l2+v)),0.0),36.0);
vec3 base=color;
if(woodMaterial){

vec2 grainP=vec2(localP.x*.045,localP.z*.32);
float warp=noise(grainP*.22)*4.0+noise(grainP*.63)*.65;
float grain=sin(localP.z*.95+warp+noise(grainP*vec2(.6,3.0))*.7);
float fibres=noise(vec2(localP.x*.025,localP.z*7.5)+warp);
float pores=pow(noise(vec2(localP.x*.1,localP.z*22.0)),10.0);
base=mix(vec3(.59,.365,.185),vec3(.77,.545,.315),.57+.10*grain+.19*(fibres-.5));
base*=1.0-pores*.16;
float depth=max(0.0,stockTop-localP.y);
float shade=1.0;
for(int i=1;i<=16;i++){
float t=float(i)*.4;
float blocker=surface(localP.xz+vec2(-.5,.7)*t)-(localP.y+.9*t);
shade=min(shade,1.0-smoothstep(.035,.30,blocker)*.69);
}
float ao=0.0;
for(int i=0;i<4;i++){
float angle=float(i)*1.57079633;
float h=surface(localP.xz+vec2(cos(angle),sin(angle))*1.0)-localP.y;
ao+=clamp(h*.14,0.0,.16);
}
base*=1.0-ao*.52;
d*=shade;
base*=1.0-.055*(1.0-max(0.0,n.y))*min(1.0,depth*.6);
}
vec3 lit=base*(0.33+0.57*d+0.23*d2)+mix(vec3(0.85),color,0.35)*metallic*(0.48*s+0.15*s2);
frag=vec4(lit,opacity);}
)";
    if(!program_.addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) ||
       !program_.addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) || !program_.link()) { renderer_="Shader error: "+program_.log(); return; }
    buildGeometry();
    if(!vao_.create() || !buffer_.create()) { renderer_="OpenGL buffer creation failed"; return; }
    QOpenGLVertexArrayObject::Binder binder(&vao_); buffer_.bind();
    buffer_.allocate(geometry_.data(),static_cast<int>(geometry_.size()*sizeof(Vertex))); program_.bind();
    program_.enableAttributeArray(0); program_.setAttributeBuffer(0,GL_FLOAT,0,3,sizeof(Vertex));
    program_.enableAttributeArray(1); program_.setAttributeBuffer(1,GL_FLOAT,3*sizeof(float),3,sizeof(Vertex));
    program_.release(); buffer_.release(); glEnable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); ready_=true;
}
void MachineView::resizeGL(int w,int h) { if(functionsReady_) glViewport(0,0,w,h); }
void MachineView::drawPart(const Part& p,const QMatrix4x4& parent) {
    program_.setUniformValue("woodMaterial",false);
    const auto model=parent*p.transform;
    program_.setUniformValue("mvp",viewProjection_*model); program_.setUniformValue("model",model);
    program_.setUniformValue("normalMatrix",model.normalMatrix()); program_.setUniformValue("color",p.color);
    program_.setUniformValue("opacity",p.opacity); program_.setUniformValue("metallic",p.metallic);
    if(p.decal) { glEnable(GL_POLYGON_OFFSET_FILL); glPolygonOffset(-1,-4); }
    const auto [begin,count]=ranges_.at(static_cast<std::size_t>(p.mesh)); glDrawArrays(GL_TRIANGLES,begin,count);
    if(p.decal) glDisable(GL_POLYGON_OFFSET_FILL);
}
void MachineView::renderNode(const Node& node,const QMatrix4x4& parent,bool transparent) {
    const auto model=parent*node.transform;
    for(const auto& p:node.parts) if((p.opacity<1)==transparent) drawPart(p,model);
    for(const auto& child:node.children) renderNode(*child,model,transparent);
}
void MachineView::paintGL() {
    if(!functionsReady_) return;
    glClearColor(.065f,.083f,.11f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if(!ready_) return;
    // 10 mm near plane retains enough depth precision for the etched grid at normal zoom.
    const bool top=orthographic_;
    QMatrix4x4 projection;
    const float aspect=static_cast<float>(width())/std::max(1,height());
    if(top) { const float half=distance_*.29f; projection.ortho(-half*aspect,half*aspect,-half,half,10,3000); }
    else projection.perspective(39,aspect,10,3000);
    const float y=yaw_*.0174532925f,p=pitch_*.0174532925f;
    const auto setup=stock_.setup();
    const QVector3D stockTarget{static_cast<float>(setup.x+setup.width/2-150),static_cast<float>(setup.bottom+setup.thickness/2),static_cast<float>(setup.y+setup.depth/2-90+cnc::Kinematics3018::tableWorldZ(snapshot_.position))};
    const QVector3D target=(stockFocus_?stockTarget:top?QVector3D{0,static_cast<float>(P::tableTop.mm),0}:QVector3D{0,195,-20})+pan_;
    const auto eye=target+QVector3D{std::sin(y)*std::cos(p),std::sin(p),std::cos(y)*std::cos(p)}*distance_;
    const QVector3D forward=(target-eye).normalized(),up=top?QVector3D{0,0,-1}:QVector3D{0,1,0};
    screenRight_=QVector3D::crossProduct(forward,up).normalized();screenUp_=QVector3D::crossProduct(screenRight_,forward).normalized();
    QMatrix4x4 view; view.lookAt(eye,target,up); viewProjection_=projection*view;
    glEnable(GL_DEPTH_TEST); glDepthMask(GL_TRUE); glDisable(GL_BLEND); program_.bind(); program_.setUniformValue("eye",eye);
    QOpenGLVertexArrayObject::Binder binder(&vao_); if(!stockFocus_) renderNode(root_,QMatrix4x4{});
    drawStock(); vao_.bind();
    for(std::size_t i=1;trailVisible_ && i<trail_.size();++i) {
        if(!trail_[i].connected) continue;
        const auto delta=trail_[i].position-trail_[i-1].position;
        if(delta.length()<.02f) continue;
        Part segment; segment.color={.08f,.85f,.73f}; segment.metallic=0;
        segment.transform.translate((trail_[i].position+trail_[i-1].position)*.5f);
        segment.transform.rotate(QQuaternion::rotationTo({0,1,0},delta.normalized())); segment.transform.scale(.6f,delta.length(),.6f);
        drawPart(segment,table_->transform);
    }
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    if(!stockFocus_) {renderNode(root_,QMatrix4x4{},true);}
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
    // Coordinate guides stay readable even with the spindle parked above the origin.
    glDisable(GL_DEPTH_TEST); if(!stockFocus_) renderNode(coordinates_,table_->transform); glEnable(GL_DEPTH_TEST); program_.release();
}
void MachineView::setMachine(const cnc::Snapshot& s) {
    snapshot_=s; const auto t=cnc::Kinematics3018::toolWorld(s.position);
    const float x=static_cast<float>(t[0]),y=static_cast<float>(cnc::Kinematics3018::tableWorldZ(s.position)),z=static_cast<float>(s.position[2]+stock_.setup().toolDatum);
    table_->transform.setToIdentity(); table_->transform.translate(0,0,y);
    xSlide_->transform.setToIdentity(); xSlide_->transform.translate(x,0,0);
    zSlide_->transform.setToIdentity(); zSlide_->transform.translate(0,z,0);
    // Show executed cutting passes; rapid approach and retract do not score the wood.
    const bool running=s.state==cnc::State::Run && s.spindle>0 && z<=stock_.setup().bottom+stock_.setup().thickness+.5;
    if(running || traceActive_) {
        const QVector3D point{x,z,-y};
        if(trail_.empty() || !traceActive_ || !running || (trail_.back().position-point).length()>.4f) {
            if(trail_.size()>=2500) trail_.erase(trail_.begin(),trail_.begin()+500);
            trail_.push_back({point,traceActive_});
        }
    }
    traceActive_=running; update();
}
void MachineView::clearTrail() { trail_.clear(); traceActive_=false; update(); }
void MachineView::setWorkpieceTransparent(bool v) { stockOpacity_=v?.35f:1.f; update(); }
void MachineView::setCamera(float y,float p,float d) { yaw_=y; pitch_=p; distance_=d;pan_={};orthographic_=p>89; update(); }
void MachineView::setPreset(float y,float p) {setCamera(y,p,stockFocus_?stockDistance():980); }
void MachineView::resetCamera() {if(stockFocus_) setCamera(20,48,stockDistance());else setCamera(28,20,980); }
void MachineView::mousePressEvent(QMouseEvent* e) {drag_=e->position().toPoint();if(e->button()==Qt::RightButton) setCursor(Qt::ClosedHandCursor);e->accept();}
void MachineView::mouseReleaseEvent(QMouseEvent* e) {unsetCursor();e->accept();}

void MachineView::mouseMoveEvent(QMouseEvent* e) {
    const auto current=e->position().toPoint(),delta=current-drag_;drag_=current;
    if(e->buttons() & Qt::RightButton) {
        const float units=distance_*(orthographic_?.58f:2*std::tan(19.5f*.0174532925f))/std::max(1,height());
        pan_+=(-screenRight_*static_cast<float>(delta.x())+screenUp_*static_cast<float>(delta.y()))*units;
        update();e->accept();
    } else if(e->buttons() & Qt::LeftButton) {
        yaw_-=delta.x()*.4f; pitch_=std::clamp(pitch_+delta.y()*.3f,-10.f,89.f);orthographic_=false;
        update();e->accept();
    }
}
void MachineView::wheelEvent(QWheelEvent* e) { distance_=std::clamp(distance_*std::pow(.999f,static_cast<float>(e->angleDelta().y())),30.f,1800.f); update(); }


void MachineView::destroyStockBuffers() {
    for(auto& tile:stockTiles_) {glDeleteVertexArrays(1,&tile.vao);glDeleteBuffers(1,&tile.vbo);glDeleteBuffers(1,&tile.ebo);}
    stockTiles_.clear();stockRevision_=0;
    if(heightTexture_) {glDeleteTextures(1,&heightTexture_);heightTexture_=0;}
}
void MachineView::drawStock() {
    const bool recreate=stockRevision_!=stock_.revision();
    if(stockRevision_!=stock_.revision()) {
        destroyStockBuffers();stockRevision_=stock_.revision();stockTiles_.resize(static_cast<std::size_t>(stock_.tilesX()*stock_.tilesY()));
        glGenTextures(1,&heightTexture_);glBindTexture(GL_TEXTURE_2D,heightTexture_);
        glTexImage2D(GL_TEXTURE_2D,0,GL_R32F,stock_.columns(),stock_.rows(),0,GL_RED,GL_FLOAT,stock_.heights().data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    auto dirty=stock_.takeDirtyTiles();
    if(recreate) {dirty.clear();for(int i=0;i<stock_.tilesX()*stock_.tilesY();++i) dirty.push_back(i);}
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,heightTexture_);
    if(!dirty.empty()) glTexSubImage2D(GL_TEXTURE_2D,0,0,0,stock_.columns(),stock_.rows(),GL_RED,GL_FLOAT,stock_.heights().data());
    for(int index:dirty) {
        auto& tile=stockTiles_[static_cast<std::size_t>(index)];
        if(tile.vao==0) {glGenVertexArrays(1,&tile.vao);glGenBuffers(1,&tile.vbo);glGenBuffers(1,&tile.ebo);}
        const auto mesh=stock_.meshTile(index);tile.count=static_cast<int>(mesh.indices.size());
        glBindVertexArray(tile.vao);glBindBuffer(GL_ARRAY_BUFFER,tile.vbo);
        glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(mesh.vertices.size()*sizeof(cnc::StockVertex)),mesh.vertices.data(),GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(cnc::StockVertex),nullptr);
        glEnableVertexAttribArray(1);glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(cnc::StockVertex),reinterpret_cast<void*>(3*sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,tile.ebo);glBufferData(GL_ELEMENT_ARRAY_BUFFER,static_cast<GLsizeiptr>(mesh.indices.size()*sizeof(std::uint32_t)),mesh.indices.data(),GL_DYNAMIC_DRAW);
    }
    const auto model=table_->transform;
    program_.setUniformValue("model",model);program_.setUniformValue("mvp",viewProjection_*model);
    program_.setUniformValue("normalMatrix",model.normalMatrix());program_.setUniformValue("color",QVector3D{.72f,.48f,.25f});
    program_.setUniformValue("metallic",.05f);program_.setUniformValue("opacity",stockOpacity_);
    program_.setUniformValue("heightMap",0);
    program_.setUniformValue("stockOrigin",QVector2D{static_cast<float>(stock_.setup().x-150),static_cast<float>(stock_.setup().y-90)});
    program_.setUniformValue("stockSize",QVector2D{static_cast<float>(stock_.setup().width),static_cast<float>(stock_.setup().depth)});
    program_.setUniformValue("stockBottom",static_cast<float>(stock_.setup().bottom));
    program_.setUniformValue("woodMaterial",true);program_.setUniformValue("stockTop",static_cast<float>(stock_.setup().bottom+stock_.setup().thickness));
    if(stockOpacity_<1) {glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);}
    for(const auto& tile:stockTiles_) {glBindVertexArray(tile.vao);glDrawElements(GL_TRIANGLES,tile.count,GL_UNSIGNED_INT,nullptr);}
    glDisable(GL_BLEND);program_.setUniformValue("woodMaterial",false);
}
void MachineView::syncSetup() {
    buildScene();buildGeometry();
    if(ready_) {makeCurrent();buffer_.bind();buffer_.allocate(geometry_.data(),static_cast<int>(geometry_.size()*sizeof(Vertex)));buffer_.release();doneCurrent();}
    clearTrail();setMachine(snapshot_);
}
void MachineView::focusStock(bool value) {
    stockFocus_=value;
    if(value) setCamera(20,48,stockDistance());
    else resetCamera();
}

float MachineView::stockDistance() const {
    return static_cast<float>(std::max({stock_.setup().width,stock_.setup().depth,stock_.setup().thickness*1.5})*2.1);
}
void MachineView::setFixtures(cnc::FixtureSetup setup) {cnc::validateFixtures(setup);fixtureSetup_=setup;syncSetup();}
QPointF MachineView::projectPoint(QVector3D point) const {
    const auto clip=viewProjection_*QVector4D(point,1);
    return {(clip.x()/clip.w()+1)*width()/2,(1-clip.y()/clip.w())*height()/2};
}
