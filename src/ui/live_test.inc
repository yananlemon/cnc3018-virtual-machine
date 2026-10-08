    void liveCarveTest(const QString& path,const QString& directory) {
        QDir().mkpath(directory);quiet_=true;mode_->setCurrentIndex(0);toggleConnection();sendLine("$H");
        for(int i=0;i<100000 && kernel_.snapshot().state==cnc::State::Home;++i) {kernel_.tick(.05);flush();}
        if(!loadPath(path,true)) {QApplication::exit(2);return;}
        focus_->setChecked(true);speed_->setCurrentIndex(4);startProgram();
        auto poll=new QTimer(this);poll->setInterval(80);auto wall=std::make_shared<QElapsedTimer>();wall->start();
        auto phase=std::make_shared<int>(0);auto paused=std::make_shared<std::vector<float>>();auto finished=std::make_shared<std::vector<float>>();
        auto report=std::make_shared<QJsonObject>(QJsonObject{{"version","0.5.0"},{"regularGuiTimer",true},{"pausePreserved",false},{"resetPreserved",false},{"repeatIdentical",false}});
        QObject::connect(poll,&QTimer::timeout,this,[=,this] {
            if(wall->elapsed()>120000 || sender_.errors() || (kernel_.snapshot().state==cnc::State::Alarm && *phase!=4)) {poll->stop();QApplication::exit(2);return;}
            if(*phase==0 && sender_.sent()>sender_.total()/3) {
                stock_.flush();(*report)["partialImage"]=view_->grabFramebuffer().save(directory+"/partial.png");realtime('!');*phase=1;
            } else if(*phase==1 && kernel_.snapshot().state==cnc::State::Hold) {
                stock_.flush();*paused=stock_.heights();*phase=2;
            } else if(*phase==2) {
                stock_.flush();(*report)["pausePreserved"]=*paused==stock_.heights();realtime('~');*phase=3;
            } else if(*phase==3 && !sender_.active() && kernel_.snapshot().state==cnc::State::Idle) {
                stock_.flush();*finished=stock_.heights();(*report)["firstRemovedMm3"]=stock_.stats().removedVolume;
                (*report)["finishedImage"]=view_->grabFramebuffer().save(directory+"/finished.png");sender_.clear();realtime(0x18);stock_.flush();
                (*report)["resetPreserved"]=*finished==stock_.heights();sendLine("$H");*phase=4;
            } else if(*phase==4 && kernel_.snapshot().state==cnc::State::Idle) {startProgram();*phase=5;}
            else if(*phase==5 && !sender_.active() && kernel_.snapshot().state==cnc::State::Idle) {
                stock_.flush();poll->stop();timer_.stop();quiet_=false;updateTelemetry();
                (*report)["repeatIdentical"]=*finished==stock_.heights();(*report)["removedMm3"]=stock_.stats().removedVolume;
                (*report)["sent"]=static_cast<qint64>(sender_.sent());(*report)["confirmed"]=static_cast<qint64>(sender_.confirmed());(*report)["errors"]=static_cast<qint64>(sender_.errors());
                (*report)["elapsedMs"]=wall->elapsed();(*report)["openglReady"]=view_->ready();(*report)["guiImage"]=grab().save(directory+"/gui.png");
                QFile file(directory+"/live.json");const bool saved=file.open(QIODevice::WriteOnly);if(saved) file.write(QJsonDocument(*report).toJson());
                QApplication::exit(saved && report->value("pausePreserved").toBool() && report->value("resetPreserved").toBool() && report->value("repeatIdentical").toBool() && view_->ready()?0:2);
            }
        });poll->start();
    }
