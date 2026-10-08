    void carveTest(const QString& path,const QString& directory,bool repeat=false,double grid=0,const QString& continuation={}) {
        timer_.stop();QDir().mkpath(directory);quiet_=true;
        mode_->setCurrentIndex(0);toggleConnection();
        sendLine("$H");
        for(int i=0;i<100000 && kernel_.snapshot().state==cnc::State::Home;++i) {kernel_.tick(.05);flush();}
        bool loaded=loadPath(path,true);
        if(loaded && grid>0) {auto setup=stock_.setup();setup.resolution=grid;stock_.configure(setup);view_->syncSetup();}
        bool completed=false;QElapsedTimer clock;clock.start();
        double firstVolume=0;bool repeatIdentical=true,continuationPreserved=true,monotonic=true;
        qint64 firstCommands=0;
        auto execute=[&] {
            startProgram();
            for(int i=0;i<20000000;++i) {
                pump();kernel_.tick(.05);flush();
                const auto state=kernel_.snapshot().state;
                if(sender_.errors() || state==cnc::State::Alarm || clock.elapsed()>300000) return false;
                if(!sender_.active() && state==cnc::State::Idle && kernel_.snapshot().rxBytes==0) {stock_.flush();return sender_.confirmed()==sender_.total();}
                if(i%10000==0) QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            return false;
        };
        if(loaded) {
            completed=execute();firstVolume=stock_.stats().removedVolume;firstCommands=static_cast<qint64>(sender_.total());
            if(!continuation.isEmpty() && completed) {
                const auto before=stock_.heights();const auto offset=kernel_.snapshot().workOffset;
                completed=loadPath(continuation,false);continuationPreserved=before==stock_.heights() && offset==kernel_.snapshot().workOffset;
                if(completed) completed=execute();
                for(std::size_t i=0;i<before.size();++i) monotonic&=stock_.heights()[i]<=before[i];
            }
            if(repeat && completed) {const auto heights=stock_.heights();completed=execute();repeatIdentical=heights==stock_.heights();}
        }
        stock_.flush();quiet_=false;updateTelemetry();focus_->setChecked(true);view_->showTrail(false);view_->setWorkpieceTransparent(false);
        log(QStringLiteral("验证结果：%1 · %2 ms").arg(completed?"完成":"失败").arg(clock.elapsed()));
        const auto stats=stock_.stats();const auto setup=stock_.setup();
        QJsonObject report{{"version","0.5.0"},{"input",path},{"loaded",loaded},{"completed",completed},{"elapsedMs",clock.elapsed()},
            {"sent",static_cast<qint64>(sender_.sent())},{"confirmed",static_cast<qint64>(sender_.confirmed())},{"total",static_cast<qint64>(sender_.total())},{"errors",static_cast<qint64>(sender_.errors())},
            {"state",cnc::Controller::stateName(kernel_.snapshot().state)},{"removedMm3",stats.removedVolume},{"firstPassRemovedMm3",firstVolume},{"maxDepthMm",stats.maxDepth},
            {"heightSamples",static_cast<qint64>(stock_.heights().size())},{"gridMm",setup.resolution},{"widthMm",setup.width},{"depthMm",setup.depth},{"thicknessMm",setup.thickness},
            {"toolDiameterMm",setup.cutter.diameter},{"toolShape",static_cast<int>(setup.cutter.shape)},{"rapidContacts",static_cast<qint64>(stats.rapidContacts)},{"spindleOffContacts",static_cast<qint64>(stats.stoppedContacts)},
            {"shankContacts",static_cast<qint64>(stats.shankContacts)},{"tableContacts",static_cast<qint64>(stats.tableContacts)},
            {"continuation",continuation},{"firstCommands",firstCommands},{"continuationPreserved",continuationPreserved},{"monotonicRemoval",monotonic},{"repeatChecked",repeat},{"repeatIdentical",repeatIdentical},{"externalComVerified",false}};
        QFile input(path);if(input.open(QIODevice::ReadOnly)) report["sha256"]=QString::fromLatin1(QCryptographicHash::hash(input.readAll(),QCryptographicHash::Sha256).toHex());
        bool exported=false;
        if(completed && stock_.heights().size()<700000) {
            try {stock_.exportStl((directory+QStringLiteral("/雕刻结果.stl")).toUtf8().toStdString());exported=true;}
            catch(const std::exception& e) {report["exportError"]=QString::fromUtf8(e.what());}
        }
        report["stlExported"]=exported;
        QTimer::singleShot(200,this,[this,directory,report,completed,repeatIdentical,continuationPreserved,monotonic]() mutable {
            const auto setup=stock_.setup();const float distance=static_cast<float>(std::max(setup.width,setup.depth)*2.1);
            const bool perspective=view_->grabFramebuffer().save(directory+"/carved-perspective.png");
            const bool gui=grab().save(directory+"/gui.png");
            view_->setCamera(0,90,distance);
            const bool top=view_->grabFramebuffer().save(directory+"/carved-top.png");
            view_->setCamera(-35,25,distance*.85f);
            const bool low=view_->grabFramebuffer().save(directory+"/carved-low.png");
            report["screenshotsSaved"]=perspective && top && low && gui;report["openglReady"]=view_->ready();report["renderer"]=view_->renderer();
            QFile file(directory+"/carving.json");const bool saved=file.open(QIODevice::WriteOnly);if(saved) file.write(QJsonDocument(report).toJson());
            QApplication::exit(completed && repeatIdentical && continuationPreserved && monotonic && perspective && top && low && gui && view_->ready() && saved && stock_.stats().removedVolume>0?0:2);
        });
    }
