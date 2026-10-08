    void setupTest(const QString& path,const QString& directory) {
        timer_.stop();quiet_=true;QDir().mkpath(directory);preferenceOverride_=QDir(directory).absoluteFilePath("saved-setup.json");
        WorkspaceSetup setup;setup.keepStock=true;setup.fileTool=false;
        setup.stock.x=60;setup.stock.y=30;setup.stock.width=180;setup.stock.depth=100;setup.stock.thickness=18;setup.stock.toolDatum=148;setup.stock.workX=70;setup.stock.workY=40;
        setup.fixtures.automatic=false;
        setup.fixtures.clamps[0]={true,true,48,80,0,32,16,18};setup.fixtures.clamps[1]={true,true,252,80,180,32,16,18};
        setup.fixtures.clamps[2]={true,true,150,18,90,32,16,18};setup.fixtures.clamps[3]={true,true,150,142,270,32,16,18};
        applyWorkspace(setup);const auto expected=encodeSetup(workspaceSetup());
        const bool loaded=loadPath(path,true),importPreserved=expected==encodeSetup(workspaceSetup());
        mode_->setCurrentIndex(0);toggleConnection();sendLine("$H");
        for(int i=0;i<100000 && kernel_.snapshot().state==cnc::State::Home;++i) {kernel_.tick(.05);flush();}
        prepareStock();program_->setPlainText("G21 G90\nG0Z5\nG0X20Y20\nM3S12000\nG1Z-2F200\nG1X80F600\nG0Z5\nM5\nM30\n");startProgram();
        for(int i=0;i<100000 && (sender_.active() || kernel_.snapshot().state!=cnc::State::Idle);++i) {pump();kernel_.tick(.05);flush();}
        stock_.flush();const auto material=stock_.heights();const auto offset=kernel_.snapshot().workOffset,position=kernel_.snapshot().position;
        auto dialogImages=std::make_shared<bool>(false);
        QTimer::singleShot(150,this,[this,directory,dialogImages]{
            auto dialog=qobject_cast<QDialog*>(QApplication::activeModalWidget());if(!dialog) return;
            auto tabs=dialog->findChild<QTabWidget*>();auto table=dialog->findChild<QTableWidget*>();
            if(!tabs || !table) {dialog->reject();return;}
            bool saved=dialog->grab().save(directory+"/stock-settings.png");tabs->setCurrentIndex(1);
            saved&=dialog->grab().save(directory+"/fixture-settings.png");
            auto x=qobject_cast<QDoubleSpinBox*>(table->cellWidget(0,1));if(x) x->setValue(42);
            tabs->setCurrentIndex(2);saved&=dialog->grab().save(directory+"/tool-settings.png");*dialogImages=saved;dialog->accept();
        });
        stockDialog();
        const bool fixturePreserved=stock_.heights()==material && kernel_.snapshot().workOffset==offset && kernel_.snapshot().position==position && view_->fixtures().clamps[0].x==42;
        const bool saved=writeSetup(preferenceOverride_);QFile preferences(preferenceOverride_);if(!preferences.open(QIODevice::ReadOnly)) {QApplication::exit(2);return;}
        const auto roundtrip=decodeSetup(QJsonDocument::fromJson(preferences.readAll()).object());const bool profilePreserved=encodeSetup(roundtrip)==encodeSetup(workspaceSetup());
        QJsonArray pans;bool allPans=true;
        focus_->setChecked(false);
        for(int test=0;test<3;++test) {
            view_->setCamera(test==1?75:28,test==2?90:20,test==1?500:980);view_->grabFramebuffer();
            const QVector3D anchor=test==2?QVector3D{0,70,0}:QVector3D{0,195,-20};const auto before=view_->projectPoint(anchor);
            const QPointF start{300,220},finish{420,270};
            QMouseEvent press(QEvent::MouseButtonPress,start,view_->mapToGlobal(start.toPoint()),Qt::RightButton,Qt::RightButton,Qt::NoModifier);QApplication::sendEvent(view_,&press);
            QMouseEvent move(QEvent::MouseMove,finish,view_->mapToGlobal(finish.toPoint()),Qt::NoButton,Qt::RightButton,Qt::NoModifier);QApplication::sendEvent(view_,&move);
            QMouseEvent release(QEvent::MouseButtonRelease,finish,view_->mapToGlobal(finish.toPoint()),Qt::RightButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(view_,&release);
            const bool image=view_->grabFramebuffer().save(directory+QString("/pan-%1.png").arg(test));const auto after=view_->projectPoint(anchor),delta=after-before;
            const bool correct=std::abs(delta.x()-120)<.5 && std::abs(delta.y()-50)<.5 && kernel_.snapshot().position==position && stock_.heights()==material;
            pans.append(QJsonObject{{"view",test},{"screenDelta",QJsonArray{delta.x(),delta.y()}},{"passed",correct},{"image",image}});allPans&=correct && image;
            view_->resetCamera();view_->grabFramebuffer();allPans&=view_->cameraPan().isNull();
        }
        focus_->setChecked(true);view_->setPreset(0,90);const bool focusImage=view_->grabFramebuffer().save(directory+"/focus-top.png");
        focus_->setChecked(false);view_->resetCamera();quiet_=false;updateTelemetry();
        const bool gui=grab().save(directory+"/gui.png");
        QJsonObject report{{"version","0.5.0"},{"customStockImportPreserved",loaded && importPreserved},{"fixtureEditPreservedCarvingAndWco",fixturePreserved},
            {"profileRoundTrip",saved && profilePreserved},{"dialogScreenshots",*dialogImages},{"panChecks",pans},{"panPassed",allPans},{"focusPresetImage",focusImage},{"guiImage",gui},{"removedMm3",stock_.stats().removedVolume},{"streamErrors",static_cast<qint64>(sender_.errors())},{"openglReady",view_->ready()}};
        QFile file(directory+"/setup-test.json");const bool written=file.open(QIODevice::WriteOnly);if(written) file.write(QJsonDocument(report).toJson());
        QApplication::exit(written && loaded && importPreserved && fixturePreserved && profilePreserved && *dialogImages && allPans && focusImage && gui && view_->ready() && stock_.stats().removedVolume>0 && sender_.errors()==0?0:2);
    }
