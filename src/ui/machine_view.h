#pragma once
#include "core/controller.h"
#include "core/stock.h"
#include "ui/mesh.h"
#include "ui/fixture_setup.h"
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QVector3D>
#include <memory>
#include <vector>

class MachineView final : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
public:
    explicit MachineView(cnc::Stock& stock,QWidget* parent=nullptr);
    void syncSetup();
    void focusStock(bool value);
    void setPreset(float yaw,float pitch);
    void setFixtures(cnc::FixtureSetup setup);
    const cnc::FixtureSetup& fixtures() const { return fixtureSetup_; }
    QPointF projectPoint(QVector3D point) const;
    QVector3D cameraPan() const { return pan_; }
    void showTrail(bool value) { trailVisible_=value;update(); }
    ~MachineView() override;
    void setMachine(const cnc::Snapshot& snapshot);
    void clearTrail();
    void resetCamera();
    void setWorkpieceTransparent(bool value);
    void setCamera(float yaw,float pitch,float distance);
    QString renderer() const { return renderer_; }
    bool ready() const { return ready_; }
protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w,int h) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    using Vertex=cnc::mesh::Vertex;
    struct Part { QMatrix4x4 transform; QVector3D color; int mesh=0; float opacity=1,metallic=0.7f; bool decal=false; };
    struct Node {
        QMatrix4x4 transform;
        std::vector<Part> parts;
        std::vector<std::unique_ptr<Node>> children;
        Node* child();
        void box(QVector3D center,QVector3D size,QVector3D color);
        void rod(QVector3D center,float radius,float length,QVector3D color,char axis='Y');
    };
    cnc::Stock& stock_;
    struct StockTile { unsigned vao=0,vbo=0,ebo=0; int count=0; };
    std::vector<StockTile> stockTiles_;
    std::uint64_t stockRevision_=0;
    unsigned heightTexture_=0;
    bool stockFocus_=false,trailVisible_=false;
    float stockOpacity_=1;
    void drawStock();
    void destroyStockBuffers();
    Node root_,coordinates_;
    Node *table_=nullptr,*xSlide_=nullptr,*zSlide_=nullptr,*spindle_=nullptr,*workpiece_=nullptr;
    QOpenGLShaderProgram program_;
    QOpenGLBuffer buffer_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject vao_;
    std::vector<Vertex> geometry_;
    std::vector<cnc::mesh::Mesh> meshes_;
    std::vector<std::pair<int,int>> ranges_;
    QMatrix4x4 viewProjection_;
    cnc::Snapshot snapshot_{};
    struct TracePoint { QVector3D position; bool connected; };
    std::vector<TracePoint> trail_;
    bool traceActive_=false;
    bool functionsReady_=false;
    QPoint drag_;
    QVector3D pan_,screenRight_{1,0,0},screenUp_{0,1,0};
    bool orthographic_=false;
    cnc::FixtureSetup fixtureSetup_;
    float stockDistance() const;
    float yaw_=28,pitch_=20,distance_=980;
    bool ready_=false;
    QString renderer_="OpenGL initializing";
    void buildScene();
    void buildGeometry();
    void renderNode(const Node& node,const QMatrix4x4& parent,bool transparent=false);
    void drawPart(const Part& part,const QMatrix4x4& parent);
    void cleanup();
    int storeMesh(cnc::mesh::Mesh mesh);
    void shaped(Node* node,int mesh,QVector3D center,QVector3D color,const QMatrix4x4& orientation=QMatrix4x4{},float metallic=0.7f);
};
