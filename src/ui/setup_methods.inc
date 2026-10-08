    bool idleForSetup() {
        if(sender_.active() || (kernel_.snapshot().state!=cnc::State::Idle && kernel_.snapshot().state!=cnc::State::Alarm)) {log(QStringLiteral("HOST：更改加工设置需要机器空闲"));return false;}
        stock_.flush();return true;
    }
    bool loadPath(const QString& path,bool fresh) {
        if(!idleForSetup()) return false;
        QFile file(path);
        if(!file.open(QIODevice::ReadOnly) || file.size()>64*1024*1024) {log(QStringLiteral("HOST：无法打开文件或超过 64 MB 限制"));return false;}
        const auto text=QString::fromUtf8(file.readAll());
        try {
            const auto parsed=analyzeProgram(text);
            if(fresh && parsed.automatic) {
                auto setup=manualStock_?stock_.setup():parsed.suggested;
                if(!fileTool_) setup.cutter=stock_.setup().cutter;
                else if(manualStock_ && parsed.toolIdentified) setup.cutter=parsed.suggested.cutter;
                stock_.configure(setup);newJob_=true;autoSetup_=true;
            }
            else if(!fresh) {if(fileTool_ && parsed.toolIdentified) stock_.changeCutter(parsed.suggested.cutter);newJob_=false;}
            else {newJob_=false;autoSetup_=false;log(QStringLiteral("HOST：文件包含 G53/G92，保留当前木板和坐标，请核对加工设置"));}
            view_->syncSetup();loadedPath_=path;program_->setPlainText(text);log(parsed.summary);
            log(fresh?(manualStock_?QStringLiteral("HOST：使用你的木板尺寸和位置建立新毛坯，夹具设置保留"):QStringLiteral("HOST：新任务已建立毛坯；运行前会准备表面 Z0，可在加工设置中调整")):QStringLiteral("HOST：接续加工保留已雕刻表面和零点，仅更新刀具候选参数"));
            updateTelemetry();return true;
        } catch(const std::exception& e) {log(QString::fromUtf8(e.what()));return false;}
    }
    void loadFile(bool fresh) {
        if(!idleForSetup()) return;
        const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("载入 G-code"),QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),"G-code (*.nc *.gcode *.tap *.ngc);;All files (*)");
        if(!path.isEmpty()) loadPath(path,fresh);
    }
    bool prepareStock() {
        if(!connected_ || !internal_ || sender_.active() || kernel_.snapshot().state!=cnc::State::Idle) {log(QStringLiteral("HOST：准备零点需要内部连接，并先 Home 至空闲"));return false;}
        const auto p=kernel_.snapshot().position;const auto& s=stock_.setup();
        sendLine("G21 G90");
        sendLine(QString("G92 X%1 Y%2 Z%3").arg(p[0]-s.workX,0,'f',6).arg(p[1]-s.workY,0,'f',6).arg(p[2]-(s.bottom+s.thickness-s.toolDatum),0,'f',6).toStdString());
        newJob_=false;return true;
    }
    void exportStock() {
        stock_.flush();const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("导出当前木板"),"carved-stock.stl","STL (*.stl)");
        if(path.isEmpty()) return;
        try {stock_.exportStl(path.toUtf8().toStdString());log(QStringLiteral("HOST：已导出真实表面：%1").arg(path));}
        catch(const std::exception& e) {log(QString::fromUtf8(e.what()));}
    }
#include "ui/settings_dialog.inc"
