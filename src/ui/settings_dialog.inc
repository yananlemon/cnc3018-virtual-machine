    WorkspaceSetup workspaceSetup() const {return {stock_.setup(),view_->fixtures(),manualStock_,fileTool_};}
    QString preferencePath() const {return preferenceOverride_.isEmpty()?QCoreApplication::applicationDirPath()+"/setup.json":preferenceOverride_;}
    bool writeSetup(const QString& path) {
        QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)) {log(QStringLiteral("HOST：无法保存装夹设置：%1").arg(path));return false;}
        const auto bytes=QJsonDocument(encodeSetup(workspaceSetup())).toJson();
        if(file.write(bytes)!=bytes.size() || !file.commit()) {log(QStringLiteral("HOST：保存装夹设置失败"));return false;}return true;
    }
    void applyWorkspace(const WorkspaceSetup& value) {
        stock_.configure(value.stock);manualStock_=value.keepStock;fileTool_=value.fileTool;
        view_->setFixtures(value.fixtures);newJob_=true;autoSetup_=true;updateTelemetry();
    }
    void loadSetupPreferences() {
        QFile file(preferencePath());if(!file.exists() || !file.open(QIODevice::ReadOnly)) return;
        try {applyWorkspace(decodeSetup(QJsonDocument::fromJson(file.readAll()).object()));}
        catch(const std::exception& e) {log(QStringLiteral("HOST：装夹设置读取失败：%1").arg(QString::fromUtf8(e.what())));}
    }
    void saveSetupFile() {
        if(!idleForSetup()) return;
        const auto path=QFileDialog::getSaveFileName(this,QStringLiteral("保存木板和夹具设置"),"my-setup.json","装夹设置 (*.json)");
        if(!path.isEmpty()) writeSetup(path);
    }
    void loadSetupFile() {
        if(!idleForSetup()) return;
        const auto path=QFileDialog::getOpenFileName(this,QStringLiteral("载入木板和夹具设置（建立新毛坯）"),{},"装夹设置 (*.json)");
        if(path.isEmpty()) return;
        QFile file(path);if(!file.open(QIODevice::ReadOnly) || file.size()>1024*1024) {log(QStringLiteral("HOST：无法打开装夹设置"));return;}
        if(!idleForSetup()) return;
        try {applyWorkspace(decodeSetup(QJsonDocument::fromJson(file.readAll()).object()));writeSetup(preferencePath());}
        catch(const std::exception& e) {log(QString::fromUtf8(e.what()));}
    }
    void stockDialog() {
        if(!idleForSetup()) return;
        QDialog dialog(this);dialog.setWindowTitle(QStringLiteral("木板 / 夹具 / 刀具"));dialog.resize(930,650);
        auto layout=new QVBoxLayout(&dialog);auto tabs=new QTabWidget;layout->addWidget(tabs);
        const auto old=stock_.setup();auto next=old;auto fixture=cnc::placedFixtures(view_->fixtures(),old);
        struct Field {QDoubleSpinBox* box;double* value;double initial,original;};std::vector<Field> inputs;
        auto field=[&](QFormLayout* form,const QString& label,double& v,double lo,double hi) {
            auto box=new QDoubleSpinBox;box->setDecimals(3);box->setRange(lo,hi);box->setValue(v);
            form->addRow(label,box);inputs.push_back({box,&v,box->value(),v});return box;
        };
        auto stockPage=new QWidget;auto stockLayout=new QVBoxLayout(stockPage);auto stockForm=new QFormLayout;stockLayout->addLayout(stockForm);
        tabs->addTab(stockPage,QStringLiteral("木板和零点"));
        auto keep=new QCheckBox(QStringLiteral("保留我的木板设置，导入 G-code 时不自动改变尺寸和位置"));keep->setChecked(true);stockLayout->addWidget(keep);
        field(stockForm,QStringLiteral("木板宽 X (mm)"),next.width,1,300);field(stockForm,QStringLiteral("木板深 Y (mm)"),next.depth,1,180);field(stockForm,QStringLiteral("木板厚度 (mm)"),next.thickness,1,60);
        auto bx=field(stockForm,QStringLiteral("木板左上角 X（台面）"),next.x,0,299);auto by=field(stockForm,QStringLiteral("木板左上角 Y（台面）"),next.y,0,179);
        auto origin=new QComboBox;origin->addItems({QStringLiteral("自定义程序原点"),QStringLiteral("程序 XY0 在木板左上角"),QStringLiteral("程序 XY0 在木板中心")});stockForm->addRow(QStringLiteral("程序原点"),origin);
        auto wx=field(stockForm,QStringLiteral("程序原点 X（台面）"),next.workX,-1000,1000);auto wy=field(stockForm,QStringLiteral("程序原点 Y（台面）"),next.workY,-1000,1000);
        QObject::connect(origin,&QComboBox::currentIndexChanged,&dialog,[=](int index){wx->setEnabled(index==0);wy->setEnabled(index==0);});
        field(stockForm,QStringLiteral("刀尖高度基准 (mm)"),next.toolDatum,100,220);field(stockForm,QStringLiteral("网格间距 (mm)"),next.resolution,.025,2);
        button(newRow(stockLayout),QStringLiteral("木板放到台面中央"),[&,bx,by]{for(auto& f:inputs) *f.value=f.box->value()!=f.initial?f.box->value():f.original;bx->setValue((300-next.width)/2);by->setValue((180-next.depth)/2);});
        auto stockNote=new QLabel(QStringLiteral("台面左上角为 X0/Y0，单位 mm；Z0 为木板上表面。修改木板尺寸、位置或网格会建立新毛坯。"));stockNote->setWordWrap(true);stockLayout->addWidget(stockNote);stockLayout->addStretch();
        auto clampPage=new QWidget;auto clampLayout=new QVBoxLayout(clampPage);tabs->addTab(clampPage,QStringLiteral("夹具"));
        auto automatic=new QCheckBox(QStringLiteral("根据木板自动布置两只夹具"));automatic->setChecked(view_->fixtures().automatic);clampLayout->addWidget(automatic);
        auto table=new QTableWidget(4,8);table->setHorizontalHeaderLabels({QStringLiteral("启用"),"X mm","Y mm",QStringLiteral("朝向 °"),QStringLiteral("长度 mm"),QStringLiteral("宽度 mm"),QStringLiteral("随板厚"),QStringLiteral("压板底高 mm")});
        table->verticalHeader()->setVisible(false);table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);table->setMinimumHeight(195);clampLayout->addWidget(table);
        std::array<QCheckBox*,4> enabled{},follow{};
        auto checkCell=[&](int row,int column,bool checked) {auto cell=new QWidget;auto rowLayout=new QHBoxLayout(cell);rowLayout->setContentsMargins(0,0,0,0);auto box=new QCheckBox;box->setChecked(checked);rowLayout->addWidget(box,0,Qt::AlignCenter);table->setCellWidget(row,column,cell);return box;};
        auto numeric=[&](int row,int column,double& value,double lo,double hi) {auto box=new QDoubleSpinBox;box->setDecimals(2);box->setRange(lo,hi);box->setValue(value);table->setCellWidget(row,column,box);inputs.push_back({box,&value,box->value(),value});return box;};
        for(int i=0;i<4;++i) {
            auto& c=fixture.clamps[static_cast<std::size_t>(i)];enabled[i]=checkCell(i,0,c.enabled);
            numeric(i,1,c.x,-15,315);numeric(i,2,c.y,-15,195);numeric(i,3,c.angle,-360,360);numeric(i,4,c.length,20,100);numeric(i,5,c.width,8,50);follow[i]=checkCell(i,6,c.followSurface);
            auto h=numeric(i,7,c.contactHeight,1,60);h->setEnabled(!c.followSurface);QObject::connect(follow[i],&QCheckBox::toggled,&dialog,[h](bool value){h->setEnabled(!value);});table->setRowHeight(i,38);
        }
        table->setEnabled(!automatic->isChecked());QObject::connect(automatic,&QCheckBox::toggled,&dialog,[table](bool value){table->setEnabled(!value);});
        auto clampNote=new QLabel(QStringLiteral("最多四只夹具。X/Y 为螺杆在台面上的位置；朝向 0° 指向 X+，90° 指向 Y+。勾选“随板厚”时压板贴合木板上表面。夹具设置不会清除雕刻。"));clampNote->setWordWrap(true);clampLayout->addWidget(clampNote);clampLayout->addStretch();
        auto toolPage=new QWidget;auto toolLayout=new QVBoxLayout(toolPage);auto toolForm=new QFormLayout;toolLayout->addLayout(toolForm);tabs->addTab(toolPage,QStringLiteral("刀具"));
        auto fileTool=new QCheckBox(QStringLiteral("导入时使用文件中已识别的刀具参数"));fileTool->setChecked(fileTool_);toolLayout->addWidget(fileTool);
        auto shape=new QComboBox;shape->addItems({QStringLiteral("平底刀"),QStringLiteral("球头刀"),QStringLiteral("V 形刀")});shape->setCurrentIndex(static_cast<int>(next.cutter.shape));toolForm->addRow(QStringLiteral("刀具形状"),shape);
        field(toolForm,QStringLiteral("刀具直径 (mm)"),next.cutter.diameter,.05,30);field(toolForm,QStringLiteral("V 刀夹角 (°)"),next.cutter.angle,5,170);field(toolForm,QStringLiteral("V 刀尖平面直径 (mm)"),next.cutter.tipDiameter,0,30);field(toolForm,QStringLiteral("刃长 (mm)"),next.cutter.cuttingLength,.1,40);field(toolForm,QStringLiteral("伸出长度 (mm)"),next.cutter.stickout,.1,40);toolLayout->addStretch();
        auto restore=new QCheckBox(QStringLiteral("重新建立毛坯（清除当前雕刻）"));layout->addWidget(restore);

        auto collect=[&]() -> WorkspaceSetup {
            for(auto& f:inputs) *f.value=f.box->value()!=f.initial?f.box->value():f.original;
            if(origin->currentIndex()==1) {next.workX=next.x;next.workY=next.y;}
            if(origin->currentIndex()==2) {next.workX=next.x+next.width/2;next.workY=next.y+next.depth/2;}
            next.cutter.shape=static_cast<cnc::CutterShape>(shape->currentIndex());fixture.automatic=automatic->isChecked();
            for(int i=0;i<4;++i) {fixture.clamps[i].enabled=enabled[i]->isChecked();fixture.clamps[i].followSurface=follow[i]->isChecked();}
            return {next,fixture,keep->isChecked(),fileTool->isChecked()};
        };
        auto profiles=new QHBoxLayout;layout->addLayout(profiles);
        button(profiles,QStringLiteral("保存此装夹…"),[&]{
            try {
                const auto value=collect();const auto object=encodeSetup(value);decodeSetup(object);
                const auto path=QFileDialog::getSaveFileName(&dialog,QStringLiteral("保存木板和夹具设置"),"my-setup.json","装夹设置 (*.json)");
                if(path.isEmpty()) return;
                QSaveFile file(path);const auto bytes=QJsonDocument(object).toJson();
                if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) log(QStringLiteral("HOST：保存装夹失败"));
            } catch(const std::exception& e) {QMessageBox::warning(&dialog,QStringLiteral("参数需调整"),QString::fromUtf8(e.what()));}
        });
        button(profiles,QStringLiteral("载入装夹…"),[this,&dialog]{dialog.reject();QTimer::singleShot(0,this,[this]{loadSetupFile();});});profiles->addStretch();
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("应用"));buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));QObject::connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept);QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
        if(dialog.exec()!=QDialog::Accepted || !idleForSetup()) return;
        collect();
        try {
            cnc::Stock validation(next);cnc::validateFixtures(fixture);
            const bool geometry=old.width!=next.width || old.depth!=next.depth || old.thickness!=next.thickness || old.x!=next.x || old.y!=next.y || old.resolution!=next.resolution || old.toolDatum!=next.toolDatum;
            const bool placement=old.workX!=next.workX || old.workY!=next.workY;
            if(geometry || restore->isChecked()) stock_.configure(next);
            else {if(old.cutter.shape!=next.cutter.shape || old.cutter.diameter!=next.cutter.diameter || old.cutter.angle!=next.cutter.angle || old.cutter.tipDiameter!=next.cutter.tipDiameter || old.cutter.cuttingLength!=next.cutter.cuttingLength || old.cutter.stickout!=next.cutter.stickout) stock_.changeCutter(next.cutter);stock_.setPlacement(next.workX,next.workY);}
            manualStock_=keep->isChecked();fileTool_=fileTool->isChecked();view_->setFixtures(fixture);
            if(geometry || placement || restore->isChecked()) {newJob_=true;autoSetup_=true;}
            updateTelemetry();writeSetup(preferencePath());
        } catch(const std::exception& e) {log(QString::fromUtf8(e.what()));}
    }
    static QHBoxLayout* newRow(QVBoxLayout* parent) {auto row=new QHBoxLayout;parent->addLayout(row);return row;}
