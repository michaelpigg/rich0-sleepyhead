/*
 Overview GUI Implementation
 Copyright (c)2011 Mark Watkins <jedimark@users.sourceforge.net>
 License: GPL
*/

#include <QCalendarWidget>
#include <QTextCharFormat>
#include <QSystemLocale>
#include <QDebug>
#include <QDateTimeEdit>
#include <QCalendarWidget>
#include <QFileDialog>
#include <QMessageBox>
//#include <QProgressBar>

#include "SleepLib/profiles.h"
#include "overview.h"
#include "ui_overview.h"
#include "common_gui.h"
#include "Graphs/gXAxis.h"
#include "Graphs/gLineChart.h"
#include "Graphs/gYAxis.h"

#include "mainwindow.h"
extern MainWindow * mainwin;
//extern QProgressBar * qprogress;

Overview::Overview(QWidget *parent,gGraphView * shared) :
    QWidget(parent),
    ui(new Ui::Overview),
    m_shared(shared)
{
    ui->setupUi(this);

    // Set Date controls locale to 4 digit years
    QLocale locale=QLocale::system();
    QString shortformat=locale.dateFormat(QLocale::ShortFormat);
    if (!shortformat.toLower().contains("yyyy")) {
        shortformat.replace("yy","yyyy");
    }
    ui->dateStart->setDisplayFormat(shortformat);
    ui->dateEnd->setDisplayFormat(shortformat);

    Qt::DayOfWeek dow=firstDayOfWeekFromLocale();

    ui->dateStart->calendarWidget()->setFirstDayOfWeek(dow);
    ui->dateEnd->calendarWidget()->setFirstDayOfWeek(dow);


    // Stop both calendar drop downs highlighting weekends in red
    QTextCharFormat format = ui->dateStart->calendarWidget()->weekdayTextFormat(Qt::Saturday);
    format.setForeground(QBrush(Qt::black, Qt::SolidPattern));
    ui->dateStart->calendarWidget()->setWeekdayTextFormat(Qt::Saturday, format);
    ui->dateStart->calendarWidget()->setWeekdayTextFormat(Qt::Sunday, format);
    ui->dateEnd->calendarWidget()->setWeekdayTextFormat(Qt::Saturday, format);
    ui->dateEnd->calendarWidget()->setWeekdayTextFormat(Qt::Sunday, format);

    // Connect the signals to update which days have CPAP data when the month is changed
    connect(ui->dateStart->calendarWidget(),SIGNAL(currentPageChanged(int,int)),SLOT(dateStart_currentPageChanged(int,int)));
    connect(ui->dateEnd->calendarWidget(),SIGNAL(currentPageChanged(int,int)),SLOT(dateEnd_currentPageChanged(int,int)));

    // Create the horizontal layout to hold the GraphView object and it's scrollbar
    layout=new QHBoxLayout(ui->graphArea);
    layout->setSpacing(0); // remove the ugly margins/spacing
    layout->setMargin(0);
    layout->setContentsMargins(0,0,0,0);
    ui->graphArea->setLayout(layout);
    ui->graphArea->setAutoFillBackground(false);

    // Create the GraphView Object
    GraphView=new gGraphView(ui->graphArea,m_shared);
    GraphView->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);

    GraphView->setEmptyText("Import some data!");
    GraphView->setCubeImage(images["nodata"]);

    // Create the custom scrollbar and attach to GraphView
    scrollbar=new MyScrollBar(ui->graphArea);
    scrollbar->setOrientation(Qt::Vertical);
    scrollbar->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Expanding);
    scrollbar->setMaximumWidth(20);
    GraphView->setScrollBar(scrollbar);


    // Add the graphView and scrollbar to the layout.
    layout->addWidget(GraphView,1);
    layout->addWidget(scrollbar,0);
    layout->layout();

    // TODO: Automate graph creation process

    // The following code (to the closing marker) is crap --->
    if (PROFILE.general->calculateRDI())
        AHI=createGraph(tr("RDI"),"Respiratory\nDisturbance\nIndex");
    else
        AHI=createGraph(tr("AHI"),tr("Apnea\nHypopnea\nIndex"));
    UC=createGraph(tr("Usage"),tr("Usage\n(hours)"));


    US=createGraph(tr("Session Times"),tr("Session Times\n(hours)"),YT_Time);
    PR=createGraph(tr("Pressure"),tr("Pressure\n(cmH2O)"));
    SET=createGraph(tr("Settings"),("Settings"));
    LK=createGraph(tr("Leaks"),tr("Leak Rate\n(L/min)"));
    NPB=createGraph(tr("% in PB"),tr("Periodic\nBreathing\n(% of night)"));
    AHIHR=createGraph(tr("AHI/Hour"),tr("AHI Events/Hour\n(ahi/hr)"));
    RR=createGraph(tr("Resp. Rate"),tr("Respiratory\nRate\n(breaths/min)"));
    TV=createGraph(tr("Tidal Volume"),tr("Tidal\nVolume\n(ml)"));
    MV=createGraph(tr("Minute Vent."),tr("Minute\nVentilation\n(L/min)"));
    PTB=createGraph(tr("Pat. Trig. Br."),tr("Patient\nTriggered\nBreaths\n(%)"));
    SES=createGraph(tr("Sessions"),tr("Sessions\n(count)"));
    PULSE=createGraph(tr("Pulse Rate"),tr("Pulse Rate\n(bpm)"));
    SPO2=createGraph(STR_TR_SpO2,tr("Oxygen Saturation\n(%)"));

    WEIGHT=createGraph(STR_TR_Weight,STR_TR_Weight,YT_Weight);
    BMI=createGraph(STR_TR_BMI,tr("Body\nMass\nIndex"));
    ZOMBIE=createGraph(tr("Zombie"),tr("How you felt\n(0-10)"));

    ahihr=new SummaryChart(tr("AHI/Hr"),GT_LINE);
    ahihr->addSlice(CPAP_AHI,QColor("blue"),ST_MAX,false);
    ahihr->addSlice(CPAP_AHI,QColor("orange"),ST_WAVG,false);
    AHIHR->AddLayer(ahihr);

    weight=new SummaryChart(STR_TR_Weight,GT_LINE);
    weight->setMachineType(MT_JOURNAL);
    weight->addSlice(Journal_Weight,QColor("black"),ST_SETAVG,true);
    WEIGHT->AddLayer(weight);

    bmi=new SummaryChart(STR_TR_BMI,GT_LINE);
    bmi->setMachineType(MT_JOURNAL);
    bmi->addSlice(Journal_BMI,QColor("dark blue"),ST_SETAVG,true);
    BMI->AddLayer(bmi);

    zombie=new SummaryChart(tr("Zombie Meter"),GT_LINE);
    zombie->setMachineType(MT_JOURNAL);
    zombie->addSlice(Journal_ZombieMeter,QColor("dark red"),ST_SETAVG,true);
    ZOMBIE->AddLayer(zombie);

    pulse=new SummaryChart(tr("Pulse Rate"),GT_LINE);
    pulse->setMachineType(MT_OXIMETER);
    pulse->addSlice(OXI_Pulse,QColor("red"),ST_WAVG,true);
    pulse->addSlice(OXI_Pulse,QColor("pink"),ST_MIN,true);
    pulse->addSlice(OXI_Pulse,QColor("orange"),ST_MAX,true);
    PULSE->AddLayer(pulse);

    spo2=new SummaryChart(STR_TR_SpO2,GT_LINE);
    spo2->setMachineType(MT_OXIMETER);
    spo2->addSlice(OXI_SPO2,QColor("cyan"),ST_WAVG,true);
    spo2->addSlice(OXI_SPO2,QColor("light blue"),ST_90P,true);
    spo2->addSlice(OXI_SPO2,QColor("blue"),ST_MIN,true);
    SPO2->AddLayer(spo2);

    uc=new SummaryChart(STR_UNIT_Hours,GT_BAR);
    uc->addSlice(NoChannel,QColor("green"),ST_HOURS,true);
    UC->AddLayer(uc);

    us=new SummaryChart(STR_UNIT_Hours,GT_SESSIONS);
    us->addSlice(NoChannel,QColor("dark blue"),ST_HOURS,true);
    us->addSlice(NoChannel,QColor("blue"),ST_SESSIONS,true);
    US->AddLayer(us);

    ses=new SummaryChart(tr("Sessions"),GT_LINE);
    ses->addSlice(NoChannel,QColor("blue"),ST_SESSIONS,true);
    SES->AddLayer(ses);

    if (PROFILE.general->calculateRDI())
        bc=new SummaryChart(tr("RDI"),GT_BAR);
    else
        bc=new SummaryChart(tr("AHI"),GT_BAR);
    bc->addSlice(CPAP_Hypopnea,QColor("blue"),ST_CPH,false);
    bc->addSlice(CPAP_Apnea,QColor("dark green"),ST_CPH,false);
    bc->addSlice(CPAP_Obstructive,QColor("#40c0ff"),ST_CPH,false);
    bc->addSlice(CPAP_ClearAirway,QColor("purple"),ST_CPH,false);
    if (PROFILE.general->calculateRDI()) {
        bc->addSlice(CPAP_RERA,QColor("gold"),ST_CPH,false);
    }
    AHI->AddLayer(bc);

    set=new SummaryChart("",GT_LINE);
    //set->addSlice(PRS1_SysOneResistSet,QColor("grey"),ST_SETAVG);
    set->addSlice(PRS1_HumidSetting,QColor("blue"),ST_SETWAVG,true);
    set->addSlice(PRS1_FlexSet,QColor("red"),ST_SETWAVG,true);
    set->addSlice(RMS9_EPR,QColor("green"),ST_SETWAVG,true);
    set->addSlice(INTP_SmartFlex,QColor("purple"),ST_SETWAVG,true);
    SET->setRecMinY(0);
    SET->setRecMaxY(5);
    SET->AddLayer(set);

    rr=new SummaryChart(tr("breaths/min"),GT_LINE);
    rr->addSlice(CPAP_RespRate,QColor("light blue"),ST_MIN,true);
    rr->addSlice(CPAP_RespRate,QColor("light green"),ST_90P,true);
    rr->addSlice(CPAP_RespRate,QColor("blue"),ST_WAVG,true);
    RR->AddLayer(rr);

    tv=new SummaryChart(tr("L/b"),GT_LINE);
    tv->addSlice(CPAP_TidalVolume,QColor("light blue"),ST_MIN,true);
    tv->addSlice(CPAP_TidalVolume,QColor("light green"),ST_90P,true);
    tv->addSlice(CPAP_TidalVolume,QColor("blue"),ST_WAVG,true);
    TV->AddLayer(tv);

    mv=new SummaryChart(tr("L/m"),GT_LINE);
    mv->addSlice(CPAP_MinuteVent,QColor("light blue"),ST_MIN,true);
    mv->addSlice(CPAP_MinuteVent,QColor("light green"),ST_90P,true);
    mv->addSlice(CPAP_MinuteVent,QColor("blue"),ST_WAVG,true);
    MV->AddLayer(mv);


    ptb=new SummaryChart(tr("%PTB"),GT_LINE);
    ptb->addSlice(CPAP_PTB,QColor("yellow"),ST_MIN,true);
    ptb->addSlice(CPAP_PTB,QColor("light gray"),ST_90P,true);
    ptb->addSlice(CPAP_PTB,QColor("orange"),ST_WAVG,true);
    PTB->AddLayer(ptb);

    pr=new SummaryChart(STR_UNIT_CMH2O,GT_LINE);
    //PR->setRecMinY(4.0);
    //PR->setRecMaxY(12.0);

    CPAPMode mode=(CPAPMode)(int)PROFILE.calcSettingsMax(CPAP_Mode,MT_CPAP,PROFILE.FirstDay(MT_CPAP),PROFILE.LastDay(MT_CPAP));

    if (mode>=MODE_BIPAP) {
        pr->addSlice(CPAP_EPAP,QColor("green"),ST_MIN,true);
        pr->addSlice(CPAP_EPAP,QColor("light green"),ST_90P,true);

        pr->addSlice(CPAP_IPAP,QColor("blue"),ST_MAX,true);
        pr->addSlice(CPAP_IPAP,QColor("light blue"),ST_90P,true);
    } else if (mode>MODE_CPAP) {
        pr->addSlice(CPAP_Pressure,QColor("dark green"),ST_WAVG,true);
        pr->addSlice(CPAP_Pressure,QColor("orange"),ST_MIN,true);
        pr->addSlice(CPAP_Pressure,QColor("red"),ST_MAX,true);
    //pr->addSlice(CPAP_Pressure,QColor("grey"),ST_90P,true);
        pr->addSlice(CPAP_Pressure,QColor("grey"),ST_PERC,true,0.95);
    } else {
        pr->addSlice(CPAP_PressureMin,QColor("grey"),ST_SETWAVG,true);
    }
    PR->AddLayer(pr);

    lk=new SummaryChart(tr("Avg Leak"),GT_LINE);
    lk->addSlice(CPAP_Leak,QColor("dark grey"),ST_PERC,false,0.8);
    lk->addSlice(CPAP_Leak,QColor("dark blue"),ST_WAVG,false);
    //lk->addSlice(CPAP_Leak,QColor("dark yellow"));
    //pr->addSlice(CPAP_IPAP,QColor("red"));
    LK->AddLayer(lk);

    NPB->AddLayer(npb=new SummaryChart(tr("% PB"),GT_BAR));
    npb->addSlice(CPAP_CSR,QColor("light green"),ST_SPH,false);
    // <--- The code to the previous marker is crap

    GraphView->LoadSettings("Overview"); //no trans
    ui->rangeCombo->setCurrentIndex(6);
}
Overview::~Overview()
{
    GraphView->SaveSettings("Overview");//no trans
    disconnect(this,SLOT(dateStart_currentPageChanged(int,int)));
    disconnect(this,SLOT(dateEnd_currentPageChanged(int,int)));
    delete ui;
}
gGraph * Overview::createGraph(QString name,QString units, YTickerType yttype)
{
    int default_height=PROFILE.appearance->graphHeight();
    gGraph *g=new gGraph(GraphView,name,units,default_height,0);

    gYAxis *yt;
    switch (yttype) {
    case YT_Time:
        yt=new gYAxisTime(true); // Time scale
        break;
    case YT_Weight:
        yt=new gYAxisWeight(PROFILE.general->unitSystem());
        break;
    default:
        yt=new gYAxis(); // Plain numeric scale
        break;
    }

    g->AddLayer(yt,LayerLeft,gYAxis::Margin);
    gXAxis *x=new gXAxis();
    x->setUtcFix(true);
    g->AddLayer(x,LayerBottom,0,gXAxis::Margin);
    g->AddLayer(new gXGrid());
    return g;
}

void Overview::ReloadGraphs()
{
    GraphView->setDay(NULL);

    on_rangeCombo_activated(ui->rangeCombo->currentIndex());
}

void Overview::ResetGraphs()
{
    QDate start=ui->dateStart->date();
    QDate end=ui->dateEnd->date();
    GraphView->setDay(NULL);
    if (start.isValid() && end.isValid()) {
        setRange(start,end);
    }
}

void Overview::RedrawGraphs()
{
    GraphView->redraw();
}

void Overview::UpdateCalendarDay(QDateEdit * dateedit,QDate date)
{
    QCalendarWidget *calendar=dateedit->calendarWidget();
    QTextCharFormat bold;
    QTextCharFormat cpapcol;
    QTextCharFormat normal;
    QTextCharFormat oxiday;
    bold.setFontWeight(QFont::Bold);
    cpapcol.setForeground(QBrush(Qt::blue, Qt::SolidPattern));
    cpapcol.setFontWeight(QFont::Bold);
    oxiday.setForeground(QBrush(Qt::red, Qt::SolidPattern));
    oxiday.setFontWeight(QFont::Bold);
    bool hascpap=p_profile->GetDay(date,MT_CPAP)!=NULL;
    bool hasoxi=p_profile->GetDay(date,MT_OXIMETER)!=NULL;
    //bool hasjournal=p_profile->GetDay(date,MT_JOURNAL)!=NULL;

    if (hascpap) {
        if (hasoxi) {
            calendar->setDateTextFormat(date,oxiday);
        } else {
            calendar->setDateTextFormat(date,cpapcol);
        }
    } else if (p_profile->GetDay(date)) {
        calendar->setDateTextFormat(date,bold);
    } else {
        calendar->setDateTextFormat(date,normal);
    }
    calendar->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);
}
void Overview::dateStart_currentPageChanged(int year, int month)
{
    QDate d(year,month,1);
    int dom=d.daysInMonth();

    for (int i=1;i<=dom;i++) {
        d=QDate(year,month,i);
        UpdateCalendarDay(ui->dateStart,d);
    }
}
void Overview::dateEnd_currentPageChanged(int year, int month)
{
    QDate d(year,month,1);
    int dom=d.daysInMonth();

    for (int i=1;i<=dom;i++) {
        d=QDate(year,month,i);
        UpdateCalendarDay(ui->dateEnd,d);
    }
}


void Overview::on_dateEnd_dateChanged(const QDate &date)
{
    qint64 d1=qint64(QDateTime(ui->dateStart->date(),QTime(0,0,0),Qt::UTC).toTime_t())*1000L;
    qint64 d2=qint64(QDateTime(date,QTime(23,59,59),Qt::UTC).toTime_t())*1000L;
    GraphView->SetXBounds(d1,d2);

}

void Overview::on_dateStart_dateChanged(const QDate &date)
{
    qint64 d1=qint64(QDateTime(date,QTime(0,0,0),Qt::UTC).toTime_t())*1000L;
    qint64 d2=qint64(QDateTime(ui->dateEnd->date(),QTime(23,59,59),Qt::UTC).toTime_t())*1000L;
    GraphView->SetXBounds(d1,d2);

}

void Overview::on_toolButton_clicked()
{
    qint64 d1=qint64(QDateTime(ui->dateStart->date(),QTime(0,10,0),Qt::UTC).toTime_t())*1000L;
    qint64 d2=qint64(QDateTime(ui->dateEnd->date(),QTime(23,0,0),Qt::UTC).toTime_t())*1000L;
    GraphView->SetXBounds(d1,d2);
}

//void Overview::on_printButton_clicked()
//{
//    mainwin->PrintReport(GraphView,STR_TR_Overview); // Must be translated the same as PrintReport checks.
//}

void Overview::ResetGraphLayout()
{
    GraphView->resetLayout();
}


void Overview::on_printDailyButton_clicked()
{
    qint64 st,et;
    GraphView->GetXBounds(st,et);

    QDate s1=QDateTime::fromTime_t(st/1000L).date();
    QDate s2=QDateTime::fromTime_t(et/1000L).date();

    int len=PROFILE.countDays(MT_UNKNOWN,s1,s2);
    if (len>7) {
        if (QMessageBox::question(this, "Woah!", "Do you really want to print "+QString::number(len)+" days worth of Daily reports,\n from "+s1.toString(Qt::SystemLocaleShortDate)+" to "+s2.toString(Qt::SystemLocaleShortDate)+"?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::No) {
            return;
        }
        if (len>14) {
            int weeks=len/7;
            if (QMessageBox::question(this, "Hold Up!", "We are talking about over "+QString::number(weeks)+" weeks of information.\n\nThis will likely take a very long time, and a heck of a lot of paper if your not printing to a PDF file.\n\nAre you really sure?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::No) {
                return;
            }
            if (len>31) {
                if (QMessageBox::question(this, "Are you serious!!??", "We are talking about printing a lot of information.\n\nIf your not printing to a PDF file, you must really hate trees.\n\nAre you really REALLY sure?",QMessageBox::Yes,QMessageBox::No)==QMessageBox::No) {
                    return;
                }
            }
        }

        mainwin->Notify("I'm not going to nag you any more, but it would probably help if I implemented this feature.. ;)");


    } else mainwin->Notify("If this was implemented yet, You'd be able to print multiple daily reports right now.");

}

void Overview::on_rangeCombo_activated(int index)
{
    QDate end=PROFILE.LastDay();
    QDate start;
    if (index==0) {
        start=end.addDays(-6);
    } else if (index==1) {
        start=end.addDays(-13);
    } else if (index==2) {
        start=end.addMonths(-1).addDays(1);
    } else if (index==3) {
        start=end.addMonths(-2).addDays(1);
    } else if (index==4) {
        start=end.addMonths(-3).addDays(1);
    } else if (index==5) {
        start=end.addMonths(-6).addDays(1);
    } else if (index==6) {
        start=end.addYears(-1).addDays(1);
    }
    if (start<PROFILE.FirstDay()) start=PROFILE.FirstDay();
    setRange(start,end);
}
void Overview::setRange(QDate start, QDate end)
{
    ui->dateEnd->blockSignals(true);
    ui->dateStart->blockSignals(true);
    ui->dateStart->setDate(start);
    ui->dateEnd->setDate(end);
    ui->dateEnd->blockSignals(false);
    ui->dateStart->blockSignals(false);
    this->on_toolButton_clicked();
}
