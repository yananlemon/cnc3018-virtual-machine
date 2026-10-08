#include "core/controller.h"
#include "core/streamer.h"
#include "transport/win_serial.h"
#include "ui/machine_view.h"
#include "ui/program.h"
#include "ui/setup_document.h"
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollArea>
#include <QSaveFile>
#include <QMouseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCryptographicHash>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QSurfaceFormat>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>

namespace {
QString demo() {
    return QStringLiteral("; v0.5 top-left machine XY0; Home first; G2 negative/G3 positive angle\n"
        "G21 G90 G17 G54\nG53 G0 X120 Y65 Z-65\nG92 X0 Y0 Z5\nM3 S8000\n"
        "G1 Z-1 F300\nG1 X60 F600\nG1 Y50\nG1 X0\nG1 Y0\n"
        "G0 Z5\nG0 X15 Y25\nG1 Z-2 F300\nG2 X45 Y25 I15 J0 F450\n"
        "G2 X15 Y25 I-15 J0\nG0 Z5\nM5\nM30\n");
}
class Window final : public QMainWindow {
public:
    Window() {
        setWindowTitle(QStringLiteral("LUNYEE 3018 Pro Ultra · Virtual Machine v0.5 · 左上 XY0")); resize(1440,960);
        auto central=new QWidget; setCentralWidget(central); auto outer=new QVBoxLayout(central);
        auto header=new QHBoxLayout;
        auto title=new QLabel(QStringLiteral("LUNYEE 3018 PRO ULTRA  /  v0.5")); title->setObjectName("title");
        header->addWidget(title); header->addStretch();
        badge_=new QLabel(QStringLiteral("未连接")); badge_->setObjectName("badge"); header->addWidget(badge_); outer->addLayout(header);
        auto hint=new QLabel(QStringLiteral("用户坐标配置：左上 XY0 · X 向右 0…300 · Y 向前/俯视向下 0…180 · Z 向上 -80…0 · Home 完成 (0,0,-2)"));
        hint->setObjectName("muted"); outer->addWidget(hint);
        auto splitter=new QSplitter; outer->addWidget(splitter,1);
        auto left=new QWidget; left->setMinimumWidth(280); left->setMaximumWidth(340);
        auto controls=new QVBoxLayout(left); controls->setContentsMargins(0,0,8,0);
        auto link=new QGroupBox(QStringLiteral("连接虚拟机器")); auto connection=new QVBoxLayout(link);
        mode_=new QComboBox; mode_->addItems({QStringLiteral("内部连接（无需驱动）"),QStringLiteral("Windows COM（端口对的机器端）")}); connection->addWidget(mode_);
        auto portsRow=new QHBoxLayout; port_=new QComboBox; port_->setEditable(true); portsRow->addWidget(port_,1);
        button(portsRow,QStringLiteral("刷新"),[this]{refreshPorts();}); connection->addLayout(portsRow);
        connect_=new QPushButton(QStringLiteral("连接")); connection->addWidget(connect_);
        QObject::connect(connect_,&QPushButton::clicked,this,[this]{toggleConnection();});
        auto serialNote=new QLabel(QStringLiteral("COM 后端：115200 / 8N1\n外部 sender 使用端口对另一端。")); serialNote->setObjectName("muted"); serialNote->setWordWrap(true); connection->addWidget(serialNote); controls->addWidget(link);
        auto stateGroup=new QGroupBox(QStringLiteral("执行状态 / 机器坐标")); auto stateLayout=new QVBoxLayout(stateGroup);
        state_=new QLabel("ALARM"); state_->setObjectName("state"); stateLayout->addWidget(state_);
        dro_=new QLabel; dro_->setObjectName("dro"); stateLayout->addWidget(dro_);
        work_=new QLabel; work_->setObjectName("muted"); stateLayout->addWidget(work_);
        telemetry_=new QLabel; telemetry_->setWordWrap(true); stateLayout->addWidget(telemetry_); controls->addWidget(stateGroup);
        auto operation=new QGroupBox(QStringLiteral("机器操作")); auto op=new QGridLayout(operation);
        gridButton(op,0,0,QStringLiteral("Home 三轴回零"),[this]{sendLine("$H");});
        gridButton(op,0,1,QStringLiteral("解锁 $X"),[this]{sendLine("$X");});
        gridButton(op,1,0,QStringLiteral("暂停 !"),[this]{realtime('!');});
        gridButton(op,1,1,QStringLiteral("恢复 ~"),[this]{realtime('~');});
        gridButton(op,2,0,QStringLiteral("Reset"),[this]{sender_.clear(); realtime(0x18);});
        gridButton(op,2,1,QStringLiteral("状态 ?"),[this]{realtime('?');});
        gridButton(op,3,0,QStringLiteral("工件零点"),[this]{newJob_=false;autoSetup_=false;sendLine("G92 X0 Y0 Z0");});
        gridButton(op,3,1,QStringLiteral("限位故障"),[this]{kernel_.hardLimit(0); sender_.stopSending(); flush();}); controls->addWidget(operation);
        auto jogGroup=new QGroupBox(QStringLiteral("Jog 点动")); auto jog=new QGridLayout(jogGroup);
        step_=new QDoubleSpinBox; step_->setRange(0.1,50); step_->setValue(5); step_->setSuffix(" mm");
        jog->addWidget(new QLabel(QStringLiteral("步长")),0,0); jog->addWidget(step_,0,1,1,2);
        jogFeed_=new QDoubleSpinBox; jogFeed_->setRange(1,1000); jogFeed_->setValue(500); jogFeed_->setSuffix(" mm/min"); jog->addWidget(jogFeed_,1,0,1,3);
        for(int axis=0;axis<3;++axis) for(int sign=0;sign<2;++sign) {
            const QString directions[3][2]={{QStringLiteral("− 左"),QStringLiteral("+ 右")},{QStringLiteral("− 后"),QStringLiteral("+ 前")},{QStringLiteral("− 降"),QStringLiteral("+ 抬")}};
            const auto label=QString("%1 %2").arg(QChar("XYZ"[axis])).arg(directions[axis][sign]);
            gridButton(jog,axis+2,sign,label,[this,axis,sign]{sendLine(QString("$J=G21G91%1%2F%3").arg(QChar("XYZ"[axis])).arg((sign?1:-1)*step_->value()).arg(jogFeed_->value()).toStdString());});
        }
        gridButton(jog,5,0,QStringLiteral("取消点动 0x85"),[this]{realtime(0x85);},2); controls->addWidget(jogGroup);
        auto note=new QLabel(QStringLiteral("Home：XY归零，Z退让2mm。\nG92只修改工件 WPos。\n解锁不建立基准。左键旋转 / 右键平移 / 滚轮缩放。")); note->setWordWrap(true); note->setObjectName("muted"); controls->addWidget(note); controls->addStretch(); splitter->addWidget(left);

        auto right=new QWidget; auto content=new QVBoxLayout(right); content->setContentsMargins(4,0,0,0);
        auto viewBar=new QHBoxLayout; viewBar->addWidget(new QLabel(QStringLiteral("台面范围：左上0,0 · X→ · Y↓"))); viewBar->addStretch();
        button(viewBar,QStringLiteral("正面"),[this]{view_->setPreset(0,12);});
        button(viewBar,QStringLiteral("背面"),[this]{view_->setPreset(180,16);});
        button(viewBar,QStringLiteral("俯视"),[this]{view_->setPreset(0,90);});
        button(viewBar,QStringLiteral("复位视角"),[this]{view_->resetCamera();}); button(viewBar,QStringLiteral("清除刀路"),[this]{view_->clearTrail();});
        auto transparency=new QCheckBox(QStringLiteral("工件半透明")); transparency->setChecked(false); viewBar->addWidget(transparency);
        focus_=new QCheckBox(QStringLiteral("工件特写"));viewBar->addWidget(focus_);
        QObject::connect(focus_,&QCheckBox::toggled,this,[this](bool value){view_->focusStock(value);});
        content->addLayout(viewBar); view_=new MachineView(stock_); content->addWidget(view_,5);
        auto arcNote=new QLabel(QStringLiteral("自定义 GRBL 1.1 子集 · Z80 标称（旧手册Z78） · G2负角/G3正角不变：当前Y↓俯视中G2逆时针、G3顺时针")); arcNote->setObjectName("muted"); content->addWidget(arcNote);
        QObject::connect(transparency,&QCheckBox::toggled,view_,&MachineView::setWorkpieceTransparent);

        auto materialBar=new QHBoxLayout;
        button(materialBar,QStringLiteral("木板 / 夹具 / 刀具"),[this]{stockDialog();});
        button(materialBar,QStringLiteral("准备表面零点"),[this]{prepareStock();});
        button(materialBar,QStringLiteral("恢复毛坯"),[this]{if(idleForSetup()) {stock_.restore();view_->clearTrail();updateTelemetry();}});
        button(materialBar,QStringLiteral("导出雕刻 STL"),[this]{exportStock();});
        speed_=new QComboBox;speed_->addItems({"1×","5×","20×","100×","1000×"});materialBar->addWidget(new QLabel(QStringLiteral("仿真速度")));materialBar->addWidget(speed_);
        auto trail=new QCheckBox(QStringLiteral("显示刀路"));materialBar->addWidget(trail);QObject::connect(trail,&QCheckBox::toggled,view_,&MachineView::showTrail);
        content->addLayout(materialBar);
        stockInfo_=new QLabel;stockInfo_->setWordWrap(true);stockInfo_->setObjectName("muted");content->addWidget(stockInfo_);
        auto tabs=new QSplitter(Qt::Horizontal); content->addWidget(tabs,2);
        auto programPanel=new QWidget; auto programLayout=new QVBoxLayout(programPanel); programLayout->setContentsMargins(0,0,4,0);
        auto programButtons=new QHBoxLayout;
        button(programButtons,QStringLiteral("载入 G-code"),[this]{loadFile(true);});
        button(programButtons,QStringLiteral("接续加工"),[this]{loadFile(false);});
        button(programButtons,QStringLiteral("示例"),[this]{if(idleForSetup()) {stock_.configure({});view_->syncSetup();newJob_=false;autoSetup_=false;loadedPath_.clear();program_->setPlainText(demo());}});
        button(programButtons,QStringLiteral("运行"),[this]{startProgram();});
        button(programButtons,QStringLiteral("停止发送"),[this]{sender_.stopSending(); log(QStringLiteral("HOST：停止后续发送；已接收的运动仍会执行。使用暂停或 Reset 控制运动。"));});
        programLayout->addLayout(programButtons); program_=new QPlainTextEdit; program_->setLineWrapMode(QPlainTextEdit::NoWrap); program_->setPlainText(demo()); programLayout->addWidget(program_);
        progress_=new QLabel(QStringLiteral("已发送 0  /  已确认 0  /  已执行运动 0")); progress_->setWordWrap(true); programLayout->addWidget(progress_); tabs->addWidget(programPanel);
        auto logPanel=new QWidget; auto logLayout=new QVBoxLayout(logPanel); logLayout->setContentsMargins(4,0,0,0);
        logLayout->addWidget(new QLabel(QStringLiteral("串口 / 协议日志"))); log_=new QPlainTextEdit; log_->setReadOnly(true); log_->setMaximumBlockCount(500); logLayout->addWidget(log_);
        auto commandRow=new QHBoxLayout; command_=new QLineEdit; command_->setPlaceholderText(QStringLiteral("单行命令，例如 $G / G1 X150 F300")); commandRow->addWidget(command_);
        auto submit=[this]{const auto text=command_->text().toStdString(); command_->clear(); sendLine(text);};
        button(commandRow,QStringLiteral("发送"),submit); QObject::connect(command_,&QLineEdit::returnPressed,this,submit); logLayout->addLayout(commandRow); tabs->addWidget(logPanel);
        splitter->addWidget(right); splitter->setStretchFactor(1,1); splitter->setSizes({300,1100});
        refreshPorts();
        elapsed_.start(); timer_.setTimerType(Qt::PreciseTimer); timer_.setInterval(5);
        QObject::connect(&timer_,&QTimer::timeout,this,[this]{service();}); timer_.start();
        if(!QApplication::arguments().contains("--smoke-test") && !QApplication::arguments().contains("--carve-test") && !QApplication::arguments().contains("--live-carve-test") && !QApplication::arguments().contains("--setup-test")) loadSetupPreferences();
        kernel_.setMotionObserver([this](const cnc::MotionStep& step){stock_.observe(step);});
        updateTelemetry();
    }

    void restoreSetupTest(const QString& directory) {
        QDir().mkpath(directory);QFile source(preferencePath());
        const bool opened=source.open(QIODevice::ReadOnly);const auto expected=opened?QJsonDocument::fromJson(source.readAll()).object():QJsonObject{};
        const bool match=opened && encodeSetup(workspaceSetup())==expected;
        const bool image=grab().save(directory+"/restored-gui.png");
        QJsonObject report{{"version","0.5.0"},{"restoredOnStartup",match},{"openglReady",view_->ready()},{"image",image}};
        QFile file(directory+"/restore-test.json");const bool saved=file.open(QIODevice::WriteOnly);if(saved) file.write(QJsonDocument(report).toJson());
        QApplication::exit(match && view_->ready() && image && saved?0:2);
    }
    MachineView* view() const { return view_; }
    void smoke(const QString& directory) {
        mode_->setCurrentIndex(0); toggleConnection(); QDir().mkpath(directory);
        QJsonArray homePhases;
        auto pose=[](const cnc::Position& p) { return QJsonArray{p[0],p[1],p[2]}; };
        sendLine("$H"); // Same send path used by the Home button.
        int lastPhase=-1;
        bool zContact=false,xyContact=false;
        for(int i=0;i<100000 && kernel_.snapshot().state==cnc::State::Home;++i) {
            const auto s=kernel_.snapshot();
            if(s.homePhase!=lastPhase) {
                homePhases.append(QJsonObject{{"phase",s.homePhase},{"machinePosition",pose(s.position)},
                    {"switches",QJsonArray{s.homeSwitches[0],s.homeSwitches[1],s.homeSwitches[2]}},
                    {"referencedAxes",QJsonArray{s.referencedAxes[0],s.referencedAxes[1],s.referencedAxes[2]}}});
                lastPhase=s.homePhase;
            }
            zContact=zContact || s.homeSwitches[2];
            xyContact=xyContact || (s.homeSwitches[0] && s.homeSwitches[1]);
            kernel_.tick(.002);
        }
        flush(); updateTelemetry();
        const auto home=kernel_.snapshot();
        const bool homeValid=home.homed && home.position==cnc::Position{0,0,-2} && home.planned==home.position &&
            zContact && xyContact && home.homeSwitches==std::array<bool,3>{} &&
            home.referencedAxes==std::array<bool,3>{true,true,true} && homePhases.size()==8;
        QTimer::singleShot(300,this,[this,directory,home,homeValid,homePhases,pose] {
            updateTelemetry(); view_->resetCamera(); view_->grabFramebuffer();
            bool homeImages=grab().save(directory+"/home-gui.png");
            view_->setPreset(0,90);
            homeImages=view_->grabFramebuffer().save(directory+"/home-top.png") && homeImages;
            QJsonArray jogResults; bool jogValid=true;
            auto jog=[&](const char* command,bool rejected,cnc::Position expected) {
                const auto before=kernel_.snapshot();
                sendLine(command); kernel_.tick(5); flush(); updateTelemetry();
                const auto after=kernel_.snapshot();
                const bool pass=(after.rejected-before.rejected)==(rejected?1u:0u) &&
                    after.position==expected && after.planned==expected && after.state==cnc::State::Idle && after.homed;
                jogValid=pass && jogValid;
                jogResults.append(QJsonObject{{"command",command},{"expectedRejection",rejected},
                    {"passed",pass},{"machinePosition",pose(after.position)}});
            };
            jog("$J=G21G91X-5F500",true,{0,0,-2});
            jog("$J=G21G91Y-5F500",true,{0,0,-2});
            jog("$J=G21G91Z5F300",true,{0,0,-2});
            jog("$J=G21G91X5F500",false,{5,0,-2});
            jog("$J=G21G91Y5F500",false,{5,5,-2});
            jog("$J=G21G91Z-5F300",false,{5,5,-7});
            view_->resetCamera(); view_->grabFramebuffer();
            homeImages=grab().save(directory+"/jog-positive-gui.png") && homeImages;
            jog("$J=G21G91X-5F500",false,{0,5,-7});
            jog("$J=G21G91Y-5F500",false,{0,0,-7});
            jog("$J=G21G91Z5F300",false,{0,0,-2});
            view_->resetCamera();
            // Also exercise existing G92, repeat execution, and Home preserving G92.
            sendLine("G92 X-10 Y-20 Z0");
            bool completed=true; QJsonArray programRuns;
            for(int run=0;run<3;++run) {
                if(run==2) { sendLine("$H"); kernel_.tick(100); flush(); }
                const auto before=kernel_.snapshot(); startProgram(); bool runCompleted=false;
                for(int i=0;i<24000;++i) {
                    pump(); kernel_.tick(.025); flush(); view_->setMachine(kernel_.snapshot());
                    const auto s=kernel_.snapshot();
                    if(!sender_.active() && sender_.sent()==sender_.total() && sender_.confirmed()==sender_.total() &&
                       s.state==cnc::State::Idle && s.queueDepth==0 && s.rxBytes==0) { runCompleted=true; break; }
                    if(s.state==cnc::State::Alarm || sender_.errors()>0) break;
                }
                const auto after=kernel_.snapshot();
                runCompleted=runCompleted && sender_.total()==17 && after.executed-before.executed==12 &&
                    after.position==cnc::Position{135,90,-65} && after.homed && sender_.errors()==0;
                programRuns.append(QJsonObject{{"run",run+1},{"afterHome",run==2},{"initialWorkOffset",pose(before.workOffset)},
                    {"initialMachinePosition",pose(before.position)},{"finalMachinePosition",pose(after.position)},
                    {"completed",runCompleted},{"sent",static_cast<int>(sender_.sent())},
                    {"confirmed",static_cast<int>(sender_.confirmed())},{"executedMotionBlocks",static_cast<int>(after.executed-before.executed)}});
                completed=runCompleted && completed;
                if(!runCompleted) break;
            }
            updateTelemetry();
            QTimer::singleShot(500,this,[this,directory,home,homeValid,homeImages,homePhases,jogValid,jogResults,completed,programRuns,pose] {
                const QImage gl=view_->grabFramebuffer(); const bool rendered=view_->ready() && !gl.isNull();
                const bool image=grab().save(directory+"/gui.png");
                const auto snapshot=kernel_.snapshot(); bool views=true;
                auto capture=[&](const char* name,float yaw,float pitch,float distance) {
                    view_->setCamera(yaw,pitch,distance);
                    views=view_->grabFramebuffer().save(directory+"/"+name+".png") && views;
                };
                capture("front",0,12,980); capture("front-left",-28,20,980);
                capture("rear",180,16,980); capture("top",0,90,980);
                auto inspection=snapshot; inspection.state=cnc::State::Idle;
                inspection.position={150,90,-cnc::Lunyee3018ProUltra::travel[2]};
                view_->setMachine(inspection); capture("z-low",35,10,880);
                inspection.position[2]=0; view_->setMachine(inspection); capture("z-high",35,10,880);
                view_->setMachine(snapshot); view_->resetCamera();
                QJsonObject report{{"version","0.5.0"},{"coordinateConfiguration","top-left XY0; X right, Y front/down"},
                    {"openglReady",rendered},{"renderer",view_->renderer()},{"screenshotSaved",image},
                    {"inspectionViewsSaved",views},{"homeScreenshotsSaved",homeImages},{"nominalTravelZ",80},
                    {"homeValidated",homeValid},{"homeMachinePosition",pose(home.position)},{"homePhases",homePhases},
                    {"jogValidated",jogValid},{"jogChecks",jogResults},{"machinePosition",pose(snapshot.position)},
                    {"state",cnc::Controller::stateName(snapshot.state)},{"homed",snapshot.homed},
                    {"sent",static_cast<int>(sender_.sent())},{"confirmed",static_cast<int>(sender_.confirmed())},
                    {"streamErrors",static_cast<int>(sender_.errors())},{"total",static_cast<int>(sender_.total())},
                    {"programCompleted",completed},{"programRuns",programRuns},{"rxBytes",static_cast<int>(snapshot.rxBytes)},
                    {"plannerDepth",static_cast<int>(snapshot.queueDepth)},
                    {"executedMotionBlocks",static_cast<int>(snapshot.executed-executedBaseline_)},
                    {"jogExecutedMotionBlocks",6},{"externalComVerified",false}};
                QFile file(directory+"/smoke.json"); const bool saved=file.open(QIODevice::WriteOnly);
                if(saved) file.write(QJsonDocument(report).toJson());
                QApplication::exit(rendered && image && views && saved && completed && homeValid && homeImages &&
                    jogValid && snapshot.homed && sender_.errors()==0 ? 0:2);
            });
        });
    }

#include "ui/carve_test.inc"
#include "ui/live_test.inc"
#include "ui/setup_test.inc"
private:
    cnc::Stock stock_;
    bool newJob_=false,autoSetup_=false,quiet_=false,manualStock_=false,fileTool_=true;
    QString loadedPath_,preferenceOverride_;
    QComboBox* speed_=nullptr;
    QCheckBox* focus_=nullptr;
    QLabel* stockInfo_=nullptr;
    cnc::Controller kernel_;
    cnc::Streamer sender_;
    cnc::WinSerial serial_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    bool connected_=false, internal_=true;
    std::uint64_t executedBaseline_=0;
    int refreshCount_=0;
    MachineView* view_=nullptr;
    QComboBox *mode_=nullptr,*port_=nullptr;
    QPushButton* connect_=nullptr;
    QLabel *badge_=nullptr,*state_=nullptr,*dro_=nullptr,*work_=nullptr,*telemetry_=nullptr,*progress_=nullptr;
    QDoubleSpinBox *step_=nullptr,*jogFeed_=nullptr;
    QPlainTextEdit *program_=nullptr,*log_=nullptr;
    QLineEdit* command_=nullptr;
    static void button(QHBoxLayout* layout,const QString& text,std::function<void()> action) {
        auto button=new QPushButton(text); layout->addWidget(button); QObject::connect(button,&QPushButton::clicked,button,std::move(action));
    }
    static void gridButton(QGridLayout* layout,int row,int column,const QString& text,std::function<void()> action,int span=1) {
        auto button=new QPushButton(text); layout->addWidget(button,row,column,1,span); QObject::connect(button,&QPushButton::clicked,button,std::move(action));
    }
    void log(const QString& text) { if(quiet_) return; log_->appendPlainText(text.trimmed()); }
    void refreshPorts() { const auto old=port_->currentText(); port_->clear(); for(auto& p:cnc::WinSerial::ports()) port_->addItem(QString::fromStdString(p)); if(!old.isEmpty()) port_->setCurrentText(old); }
    void toggleConnection() {
        if(connected_) {
            serial_.close(); sender_.stopSending(); connected_=false;
            log(QStringLiteral("HOST：已断开；控制器运动队列保留。重新连接可查询状态。"));
        } else {
            internal_=mode_->currentIndex()==0;
            if(!internal_ && !serial_.open(port_->currentText().toStdString())) { log(QString::fromStdString(serial_.error())); return; }
            // On reconnect, stale internal sender credits cannot describe a new host session.
            sender_.clear(); connected_=true; kernel_.connect(); flush();
            log(internal_?QStringLiteral("HOST：内部连接已建立"):QStringLiteral("HOST：机器串口 %1 已打开，外部 sender 请连接配对另一端").arg(port_->currentText()));
        }
        mode_->setEnabled(!connected_); port_->setEnabled(!connected_); connect_->setText(connected_?QStringLiteral("断开"):QStringLiteral("连接"));
        updateTelemetry();
    }
    void sendLine(const std::string& text) {
        if(!connected_) { log(QStringLiteral("HOST：请先连接")); return; }
        if(!internal_) { log(QStringLiteral("HOST：COM 模式的行命令由外部 sender 发送；本机按钮只允许实时命令")); return; }
        if(sender_.active()) { log(QStringLiteral("HOST：程序流正在使用确认账本；可使用暂停、恢复、Reset")); return; }
        if(text=="$H") log(QStringLiteral("HOST：健康限位回零：Z上端定位/退让2mm → XY负端定位/退让，完成机器 MPos (0,0,-2)。G92工件偏置保留。"));
        if(text.rfind("G92",0)==0) log(QStringLiteral("HOST：G92只改变工件坐标/偏置，不移动机器，也不更改机器 MPos。"));
        log("TX → "+QString::fromStdString(text)); kernel_.ingest(text+"\n"); flush();
    }
    void realtime(unsigned char c) {
        if(!connected_) { log(QStringLiteral("HOST：请先连接")); return; }
        log(QStringLiteral("TX → realtime 0x%1").arg(c,2,16,QChar('0')));
        kernel_.ingest(std::string(1,static_cast<char>(c))); flush();
    }
    void flush() {
        for(auto& message:kernel_.takeOutput()) {
            log("RX ← "+QString::fromStdString(message));
            if(message.rfind("error:15",0)==0) log(QStringLiteral("HOST：本次点动目标越界，指令未接受。先前接受的点动仍可能执行。机器范围 X[0,300] / Y[0,180] / Z[-80,0]；Z=-2时最多抬刀2mm。"));
            if(message.rfind("ALARM:2",0)==0) log(QStringLiteral("HOST：软限位报警，检查机器 MPos 目标（含工件偏置）：X[0,300] / Y[0,180] / Z[-80,0]，再回零。"));
            if(connected_ && !internal_) serial_.send(message);
            const bool ok=message=="ok\r\n",error=message.rfind("error:",0)==0;
            if(internal_ && (ok || error)) sender_.acknowledge(ok);
            if(message.rfind("ALARM:",0)==0) sender_.stopSending();
            if(message.rfind("Grbl ",0)==0) {sender_.clear();newJob_=autoSetup_;}
        }
    }
    void startProgram() {
        if(!connected_ || !internal_) { log(QStringLiteral("HOST：内置程序发送器需要内部连接模式")); return; }
        if(sender_.active() || kernel_.snapshot().state!=cnc::State::Idle) { log(QStringLiteral("HOST：请等待机器空闲，先执行 Home 或解锁")); return; }

        try {
            const auto parsed=analyzeProgram(program_->toPlainText());
            if(newJob_ && !prepareStock()) return;
            sender_.start(parsed.lines);executedBaseline_=kernel_.snapshot().executed;view_->clearTrail();
        } catch(const std::exception& e) {log(QString::fromUtf8(e.what()));}
    }
    void pump() {
        if(!connected_ || !internal_) return;
        // Burst constrained by outstanding bytes; line responses alone release credit.
        while(auto line=sender_.next()) { log("TX → "+QString::fromStdString(*line)); kernel_.ingest(*line); }
        flush();
    }
#include "ui/setup_methods.inc"
    void service() {
        const double dt=std::min(0.05,elapsed_.restart()/1000.0);
        if(connected_ && !internal_) {
            const auto bytes=serial_.poll(); if(!bytes.empty()) { log("TX → "+QString::fromLatin1(bytes.data(),static_cast<int>(bytes.size()))); kernel_.ingest(bytes); }
            if(!serial_.isOpen()) { log(QString::fromStdString(serial_.error())); connected_=false; connect_->setText(QStringLiteral("连接")); mode_->setEnabled(true); port_->setEnabled(true); }
        }
        const double rates[]{1,5,20,100,1000};
        kernel_.tick(dt*(internal_?rates[speed_->currentIndex()]:1)); flush(); pump(); view_->setMachine(kernel_.snapshot());
        if(++refreshCount_%7==0) {stock_.flush();updateTelemetry();}
    }
    void updateTelemetry() {

        const auto s=kernel_.snapshot(); view_->setMachine(s);
        const auto& material=stock_.stats();const auto& setup=stock_.setup();
        stockInfo_->setText((manualStock_?QStringLiteral("自定义木板 · "):QStringLiteral("自动木板 · "))+QStringLiteral("木板 %1×%2×%3 mm · 网格 %4 mm · 去除 %5 mm³ · 深度 %6 mm · 接触：快速 %7 / 未开主轴 %8 / 刀杆 %9 / 台面 %10").arg(setup.width,0,'f',1).arg(setup.depth,0,'f',1).arg(setup.thickness).arg(setup.resolution,0,'f',3).arg(material.removedVolume,0,'f',1).arg(material.maxDepth,0,'f',3).arg(material.rapidContacts).arg(material.stoppedContacts).arg(material.shankContacts).arg(material.tableContacts));
        badge_->setText(connected_?(internal_?QStringLiteral("● 内部连接"):QStringLiteral("● COM 机器端")):QStringLiteral("○ 未连接"));
        state_->setText(QString::fromLatin1(cnc::Controller::stateName(s.state)));
        dro_->setText(QString("机器 MPos\nX  %1 mm\nY  %2 mm\nZ  %3 mm").arg(s.position[0],8,'f',3).arg(s.position[1],8,'f',3).arg(s.position[2],8,'f',3));
        work_->setText(QString("工件 WPos  %1 / %2 / %3").arg(s.position[0]-s.workOffset[0],0,'f',2).arg(s.position[1]-s.workOffset[1],0,'f',2).arg(s.position[2]-s.workOffset[2],0,'f',2));
        telemetry_->setText(QStringLiteral("基准：%1\n实际速度：%2 mm/min\n运动队列：%3 / 15 · RX：%4 / 127\n主轴：%5 rpm").arg(s.homed?QStringLiteral("已回零"):QStringLiteral("未回零 / 不可信")).arg(s.velocity*60,0,'f',1).arg(s.queueDepth).arg(s.rxBytes).arg(s.spindle,0,'f',0));
        if(s.state==cnc::State::Home) {
            const QString phases[]{QStringLiteral("快速找开关"),QStringLiteral("退让释放"),QStringLiteral("慢速定位/建立基准"),QStringLiteral("最终退让")};
            telemetry_->setText(QStringLiteral("回零 %1：%2\n开关 X%3 Y%4 Z%5\n实际速度：%6 mm/min\n完成停靠：XY=0 / Z=-2").arg(s.homePhase<4?"Z":"XY").arg(phases[s.homePhase%4]).arg(s.homeSwitches[0]?1:0).arg(s.homeSwitches[1]?1:0).arg(s.homeSwitches[2]?1:0).arg(s.velocity*60,0,'f',1));
        }
        progress_->setText(QStringLiteral("已发送 %1 / %2  ·  已确认 %3  ·  已执行运动 %4\n未确认字节 %5  ·  错误 %6").arg(sender_.sent()).arg(sender_.total()).arg(sender_.confirmed()).arg(s.executed-executedBaseline_).arg(sender_.inflight()).arg(sender_.errors()));
        statusBar()->showMessage(view_->renderer()+QStringLiteral("  |  真实材料去除 · 平刀/球刀/V刀 · 左上XY0  |  逐段停稳，无 GRBL 前瞻规划"));
    }
};
}
int main(int argc,char** argv) {
    QSurfaceFormat format; format.setVersion(3,3); format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24); format.setSamples(4); QSurfaceFormat::setDefaultFormat(format);
    QApplication app(argc,argv); QApplication::setApplicationName("CNC3018 Virtual Machine");
    app.setStyle("Fusion");
    app.setStyleSheet(QStringLiteral(
        "QWidget{background:#151e28;color:#d8e4ed;font-family:'Microsoft YaHei UI';font-size:12px;}"
        "QMainWindow,QStatusBar{background:#101720;} QGroupBox{border:1px solid #2b3c4d;border-radius:6px;margin-top:10px;padding:12px 8px 8px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;color:#93bacf;}"
        "QPushButton{background:#263849;border:1px solid #3b5369;border-radius:4px;padding:6px 9px;}"
        "QPushButton:hover{background:#345370;} QPushButton:pressed{background:#168d82;} QPushButton:disabled{color:#677481;}"
        "QPlainTextEdit,QLineEdit,QComboBox,QDoubleSpinBox{background:#0f1720;border:1px solid #344759;border-radius:4px;padding:4px;}"
        "QPlainTextEdit{font-family:'Consolas';font-size:12px;} QLabel#title{font-size:23px;font-weight:700;letter-spacing:2px;}"
        "QLabel#badge{color:#3bd4be;padding:8px;background:#1d3440;border-radius:4px;} QLabel#muted{color:#7f9baa;}"
        "QLabel#state{font-size:20px;color:#eac179;font-weight:700;} QLabel#dro{font-family:'Consolas';font-size:19px;color:#d8f6f3;}"
        "QSplitter::handle{background:#243343;width:3px;}"));
    Window window; window.show();
    const auto arguments=app.arguments();
    const auto smokeIndex=arguments.indexOf("--smoke-test");
    if(smokeIndex>=0 && smokeIndex+1<arguments.size()) QTimer::singleShot(500,&window,[&window,arguments,smokeIndex]{window.smoke(arguments[smokeIndex+1]);});

    const auto carveIndex=arguments.indexOf("--carve-test");
    if(carveIndex>=0 && carveIndex+2<arguments.size()) QTimer::singleShot(500,&window,[&window,arguments,carveIndex]{window.carveTest(arguments[carveIndex+1],arguments[carveIndex+2],arguments.contains("--repeat"),arguments.contains("--grid")?arguments.value(arguments.indexOf("--grid")+1).toDouble():0,arguments.contains("--continue")?arguments.value(arguments.indexOf("--continue")+1):QString{});});

    const auto liveIndex=arguments.indexOf("--live-carve-test");
    if(liveIndex>=0 && liveIndex+2<arguments.size()) QTimer::singleShot(500,&window,[&window,arguments,liveIndex]{window.liveCarveTest(arguments[liveIndex+1],arguments[liveIndex+2]);});

    const auto setupIndex=arguments.indexOf("--setup-test");
    if(setupIndex>=0 && setupIndex+2<arguments.size()) QTimer::singleShot(500,&window,[&window,arguments,setupIndex]{window.setupTest(arguments[setupIndex+1],arguments[setupIndex+2]);});

    const auto restoreIndex=arguments.indexOf("--restore-setup-test");
    if(restoreIndex>=0 && restoreIndex+1<arguments.size()) QTimer::singleShot(600,&window,[&window,arguments,restoreIndex]{window.restoreSetupTest(arguments[restoreIndex+1]);});
    return app.exec();
}
