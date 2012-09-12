#include "toolmainwindow.h"
#include <QToolBar>
#include <QAction>
#include <QActionGroup>
#include <QStatusBar>
#include <QLabel>
#include <QDockWidget>
#include <QStatusBar>
#include <QKeySequence>
#include <QMenu>
#include "rotationtoolbutton.h"
#include "tooldockwidget.h"
#include <QDebug>

ActionGroup::ActionGroup(QObject *parent) : QObject(parent)
{
    current = 0;
}

QList<QAction *> ActionGroup::actions() const
{
    return m_actions;
}

void ActionGroup::addAction(QAction *action)
{
    if(!m_actions.contains(action)) {
        m_actions.append(action);
        QObject::connect(action, SIGNAL(changed()), this, SLOT(actionChanged()));
    }
    if (current && current->isChecked()) {
        current->setChecked(false);
    }
    if (action->isChecked()) {
        current = action;
    }
}

void ActionGroup::removeAction(QAction *action)
{
    if (m_actions.removeAll(action)) {
        if (action == current)
            current = 0;
        QObject::disconnect(action, SIGNAL(changed()), this, SLOT(actionChanged()));
    }
}

QAction * ActionGroup::checkedAction () const
{
    return current;
}

void ActionGroup::actionChanged()
{
    QAction *action = qobject_cast<QAction*>(sender());
    Q_ASSERT_X(action != 0, "ActionGroup::actionChanged", "internal error");
    if (action->isChecked()) {
        if (action != current) {
            if(current)
                current->setChecked(false);
            current = action;
        }
    } else if (action == current) {
        current = 0;
    }
}

ActionToolBar::ActionToolBar(QObject *parent, Qt::DockWidgetArea _area)
    : QObject(parent), area(_area), bHideToolBar(false)
{
    toolBar = new QToolBar;
    toolBar->hide();
    toolBar->setMovable(false);

    QWidget *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    spacerAct = toolBar->addWidget(spacer);
    toolBar->addSeparator();
    QWidget *spacer2 = new QWidget;
    spacer2->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    toolBar->addWidget(spacer2);

    dock1 = new ToolDockWidget;
    dock1->setObjectName(QString("dock_%1").arg(area));
    dock1->setWindowTitle(QString("dock_%1").arg(area));
    dock1->setFeatures(QDockWidget::DockWidgetClosable);
    dock1->hide();
    dock1->createMenu(area,false);

    dock2 = new ToolDockWidget;
    dock2->setObjectName(QString("dock_%1_split").arg(area));
    dock2->setWindowTitle(QString("dock_%1_split").arg(area));
    dock2->setFeatures(QDockWidget::DockWidgetClosable);
    dock2->hide();
    dock2->createMenu(area,true);

    connect(dock1,SIGNAL(visibilityChanged(bool)),this,SLOT(dock1Visible(bool)));
    connect(dock2,SIGNAL(visibilityChanged(bool)),this,SLOT(dock2Visible(bool)));
    connect(dock1,SIGNAL(moveActionTo(Qt::DockWidgetArea,QAction*,bool)),this,SIGNAL(moveActionTo(Qt::DockWidgetArea,QAction*,bool)));
    connect(dock2,SIGNAL(moveActionTo(Qt::DockWidgetArea,QAction*,bool)),this,SIGNAL(moveActionTo(Qt::DockWidgetArea,QAction*,bool)));
}

ToolDockWidget *ActionToolBar::dock(bool split) const
{
    return split?dock2:dock1;
}

void ActionToolBar::addAction(QAction *action, const QString &title, bool split)
{
    RotationToolButton *btn = new RotationToolButton;
    btn->setDefaultAction(action);
    if (area == Qt::LeftDockWidgetArea) {
        btn->setRotation(RotationToolButton::CounterClockwise);
    } else if (area == Qt::RightDockWidgetArea) {
        btn->setRotation(RotationToolButton::Clockwise);
    }
    m_actionWidgetMap.insert(action,btn);
    if (split) {
        dock2->addAction(action,title);
        toolBar->addWidget(btn);
    } else {
        dock1->addAction(action,title);
        toolBar->insertWidget(spacerAct,btn);
    }
    if (toolBar->isHidden() && !bHideToolBar) {
        toolBar->show();
    }
}

void ActionToolBar::removeAction(QAction *action, bool split)
{
    QWidget *widget = m_actionWidgetMap.value(action);
    if (widget) {
        delete widget;
    }
    m_actionWidgetMap.remove(action);
    if (split) {
        dock2->removeAction(action);
    } else {
        dock1->removeAction(action);
    }
    if (dock1->actions().isEmpty() && dock2->actions().isEmpty()) {
        toolBar->hide();
    }
}

void ActionToolBar::setHideToolBar(bool b)
{
    bHideToolBar = b;
    if (bHideToolBar) {
        toolBar->hide();
    } else {
        if (!dock1->actions().isEmpty() || !dock2->actions().isEmpty()){
            toolBar->show();
        }
    }
}

void ActionToolBar::dock1Visible(bool b)
{
    QAction *action = dock1->checkedAction();
    if (action) {
        action->setChecked(b);
    }
}

void ActionToolBar::dock2Visible(bool b)
{
    QAction *action = dock2->checkedAction();
    if (action) {
        action->setChecked(b);
    }
}


ToolMainWindow::ToolMainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_areaToolBar.insert(Qt::TopDockWidgetArea,new ActionToolBar(this,Qt::TopDockWidgetArea));
    m_areaToolBar.insert(Qt::BottomDockWidgetArea,new ActionToolBar(this,Qt::BottomDockWidgetArea));
    m_areaToolBar.insert(Qt::LeftDockWidgetArea,new ActionToolBar(this,Qt::LeftDockWidgetArea));
    m_areaToolBar.insert(Qt::RightDockWidgetArea,new ActionToolBar(this,Qt::RightDockWidgetArea));

    QMapIterator<Qt::DockWidgetArea,ActionToolBar*> it(m_areaToolBar);
    while(it.hasNext()) {
        it.next();
        Qt::DockWidgetArea area = it.key();
        ActionToolBar *actionToolBar = it.value();
        addToolBar((Qt::ToolBarArea)area,actionToolBar->toolBar);
        addDockWidget(area,actionToolBar->dock1);
        addDockWidget(area,actionToolBar->dock2);
        if (area == Qt::TopDockWidgetArea || area == Qt::BottomDockWidgetArea)
            splitDockWidget(actionToolBar->dock1,actionToolBar->dock2,Qt::Horizontal);
        else
            splitDockWidget(actionToolBar->dock1,actionToolBar->dock2,Qt::Vertical);
        connect(actionToolBar->toolBar,SIGNAL(customContextMenuRequested(QPoint)),this,SLOT(dockContextMenu(QPoint)));
        connect(actionToolBar,SIGNAL(moveActionTo(Qt::DockWidgetArea,QAction*,bool)),this,SLOT(moveToolWindow(Qt::DockWidgetArea,QAction*,bool)));
    }

    this->setDockNestingEnabled(true);
    this->setDockOptions(QMainWindow::AllowNestedDocks);

    m_statusBar = new QStatusBar;

    m_dockLockAct = new QAction(tr("-"),this);
    m_dockLockAct->setCheckable(true);

    QToolButton *btn = new QToolButton;
    btn->setDefaultAction(m_dockLockAct);

    m_statusBar->addWidget(btn);
    ActionToolBar *bar = m_areaToolBar.value(Qt::BottomDockWidgetArea);
    bar->toolBar->setStyleSheet("QToolBar {border:0}");
    m_statusBar->addWidget(bar->toolBar,1);

    //this->setStatusBar(m_statusBar);

    this->setStyleSheet("QMainWindow::separator{width:1; background-color: gray ;}");
    m_statusBar->setStyleSheet("QStatusBar {border-top: 1px solid gray}");
    /*
    this->setStyleSheet("QToolBar {border:1 ; background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 #EEEEEE, stop: 1 #ababab); color : #EEEEEE}"
                        "QStatusBar {border:1 ; background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1,stop: 0 #EEEEEE, stop: 1 #ababab); color : #EEEEEE}"
                        "QMainWindow::separator{width:1; background-color: #ababab ;}");
    */

    connect(m_dockLockAct,SIGNAL(toggled(bool)),this,SLOT(lockToolWindows(bool)));
}

ToolMainWindow::~ToolMainWindow()
{

    qDeleteAll(m_actStateMap);
}

void ToolMainWindow::removeAllToolWindows()
{
    foreach(ActionState *state, m_actStateMap.values()) {
        delete state->widget;
    }
}

void ToolMainWindow::toggledAction(bool)
{
    QAction *action = static_cast<QAction*>(sender());
    if (!action) {
        return;
    }
    ActionState *state = m_actStateMap.value(action);
    if (!state) {
        return;
    }
    ToolDockWidget *dock = m_areaToolBar.value(state->area)->dock(state->split);
    if (action->isChecked()) {
        if (dock->isHidden()) {
            dock->show();
        }
        dock->setWidget(state->widget);
        dock->setWindowTitle(state->title);
    } else {
        if (!dock->checkedAction()) {
            dock->hide();
        }
    }
}

QAction *ToolMainWindow::findToolWindow(QWidget *widget)
{
    QMapIterator<QAction*,ActionState*> it(m_actStateMap);
    while (it.hasNext()) {
        it.next();
        if (it.value()->widget == widget) {
            return it.key();
        }
    }
    return NULL;
}

void ToolMainWindow::removeToolWindow(QAction *action)
{
    ActionState *state = m_actStateMap.value(action);
    if (!state) {
        return;
    }
    if (action->isChecked()) {
        action->setChecked(false);
    }
    ActionToolBar *actToolBar = m_areaToolBar.value(state->area);
    if (actToolBar) {
        actToolBar->removeAction(action,state->split);
    }
}

QAction *ToolMainWindow::addToolWindow(Qt::DockWidgetArea area, QWidget *widget, const QString &id, const QString &title, bool split)
{
    QMap<QString,InitToolSate>::iterator it = m_idStateMap.find(id);
    if (it != m_idStateMap.end()) {
        area = it.value().area;
        split = it.value().split;
    }

    ActionToolBar *actToolBar = m_areaToolBar.value(area);
    QAction *action = new QAction(this);
    action->setText(title);
    action->setCheckable(true);

    ActionState *state = new ActionState;
    state->area = area;
    state->split = split;
    state->widget = widget;
    state->id = id;
    state->title = title;

    actToolBar->addAction(action,title,split);
    m_actStateMap.insert(action,state);

    int index = m_actStateMap.size();
    if (index <= 9) {
        action->setText(QString("&%1: %2").arg(index).arg(title));
        action->setToolTip(QString("ToolWindow \"%1\"\tATL+%2").arg(title).arg(index));
        action->setShortcut(QKeySequence(QString("ALT+%1").arg(index)));
    }

    connect(action,SIGNAL(toggled(bool)),this,SLOT(toggledAction(bool)));

    action->setChecked(true);
    return action;
}

void ToolMainWindow::moveToolWindow(Qt::DockWidgetArea area,QAction *action,bool split)
{
    ActionState *state = m_actStateMap.value(action);
    if (!state) {
        return;
    }
    if (state->area == area && state->split == split) {
        return;
    }
    ActionToolBar *actionToolBar = m_areaToolBar.value(area);
    ActionToolBar *oldActToolBar = m_areaToolBar.value(state->area);

    if (action->isChecked()) {
        action->setChecked(false);
    }

    oldActToolBar->removeAction(action,state->split);
    actionToolBar->addAction(action,state->title,split);

    state->area = area;
    state->split = split;
    action->setChecked(true);
}

void ToolMainWindow::restoreToolWindows(){
    foreach(QAction *action,m_hideActionList) {
        action->setChecked(true);
    }
    m_hideActionList.clear();
}

void ToolMainWindow::hideAllToolWindows()
{
    m_hideActionList.clear();
    foreach(QAction *action, m_actStateMap.keys()) {
        if (action->isChecked()) {
            m_hideActionList.append(action);
            action->setChecked(false);
        }
    }
}

void ToolMainWindow::lockToolWindows(bool b)
{
    QMapIterator<Qt::DockWidgetArea,ActionToolBar*> it(m_areaToolBar);
    while (it.hasNext()) {
        it.next();
        it.value()->setHideToolBar(b);
    }
}

static int VersionMarker = 0xffe0;

QByteArray ToolMainWindow::saveToolState(int version) const
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream << VersionMarker;
    stream << version;
    QMapIterator<QAction*,ActionState*> it(m_actStateMap);
    while (it.hasNext()) {
        it.next();
        ActionState *state = it.value();
        stream << state->id;
        stream << (int)state->area;
        stream << state->split;
    }
    return data;
}

bool ToolMainWindow::loadInitToolState(const QByteArray &state, int version)
{
    if (state.isEmpty())
        return false;

    QByteArray sd = state;
    QDataStream stream(&sd, QIODevice::ReadOnly);
    int marker, v;
    stream >> marker;
    stream >> v;
    if (stream.status() != QDataStream::Ok || marker != VersionMarker || v != version)
        return false;

    QString id;
    InitToolSate value;
    int area;
    while(!stream.atEnd()) {
        stream >> id;
        stream >> area;
        value.area = (Qt::DockWidgetArea)area;
        stream >> value.split;
        m_idStateMap.insert(id,value);
    }
    if (stream.status() != QDataStream::Ok) {
        m_idStateMap.clear();
        return false;
    }
    return true;

}