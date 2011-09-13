/**************************************************************************
** This file is part of LiteIDE
**
** Copyright (c) 2011 LiteIDE Team. All rights reserved.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.
**
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
**
** In addition, as a special exception,  that plugins developed for LiteIDE,
** are allowed to remain closed sourced and can be distributed under any license .
** These rights are included in the file LGPL_EXCEPTION.txt in this package.
**
**************************************************************************/
// Module: gdbdebuger.cpp
// Creator: visualfc <visualfc@gmail.com>
// date: 2011-8-12
// $Id: gdbdebuger.cpp,v 1.0 2011-8-12 visualfc Exp $

#include "gdbdebugger.h"
#include "fileutil/fileutil.h"
#include "liteapi/litefindobj.h"

#include <QStandardItemModel>
#include <QProcess>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDebug>


//lite_memory_check_begin
#if defined(WIN32) && defined(_MSC_VER) &&  defined(_DEBUG)
     #define _CRTDBG_MAP_ALLOC
     #include <stdlib.h>
     #include <crtdbg.h>
     #define DEBUG_NEW new( _NORMAL_BLOCK, __FILE__, __LINE__ )
     #define new DEBUG_NEW
#endif
//lite_memory_check_end

GdbDebugeer::GdbDebugeer(LiteApi::IApplication *app, QObject *parent) :
    LiteApi::IDebugger(parent),
    m_liteApp(app),
    m_envManager(0)
{
    m_process = new QProcess(this);
    m_executionModel = new QStandardItemModel(0,5,this);
    m_executionModel->setHeaderData(0,Qt::Horizontal,tr("Address"));
    m_executionModel->setHeaderData(1,Qt::Horizontal,tr("Function"));
    m_executionModel->setHeaderData(2,Qt::Horizontal,tr("File"));
    m_executionModel->setHeaderData(3,Qt::Horizontal,tr("Line"));
    m_executionModel->setHeaderData(4,Qt::Horizontal,tr("Thread ID"));

    m_localsModel = new QStandardItemModel(0,3,this);
    m_localsModel->setHeaderData(0,Qt::Horizontal,tr("Name"));
    m_localsModel->setHeaderData(1,Qt::Horizontal,tr("Type"));
    m_localsModel->setHeaderData(2,Qt::Horizontal,tr("Value"));

    m_framesModel = new QStandardItemModel(0,5,this);
    m_framesModel->setHeaderData(0,Qt::Horizontal,tr("Level"));
    m_framesModel->setHeaderData(1,Qt::Horizontal,tr("Address"));
    m_framesModel->setHeaderData(2,Qt::Horizontal,tr("Function"));
    m_framesModel->setHeaderData(3,Qt::Horizontal,tr("File"));
    m_framesModel->setHeaderData(4,Qt::Horizontal,tr("Line"));

    m_gdbinit = false;    

    connect(app,SIGNAL(loaded()),this,SLOT(appLoaded()));
    connect(m_process,SIGNAL(started()),this,SIGNAL(debugStarted()));
    connect(m_process,SIGNAL(finished(int)),this,SIGNAL(debugStoped()));
    connect(m_process,SIGNAL(readyReadStandardError()),this,SLOT(readStdError()));
    connect(m_process,SIGNAL(readyReadStandardOutput()),this,SLOT(readStdOutput()));
}

GdbDebugeer::~GdbDebugeer()
{
    if (m_process) {
         delete m_process;
    }
}

void GdbDebugeer::appLoaded()
{
    m_envManager = LiteApi::findExtensionObject<LiteApi::IEnvManager*>(m_liteApp,"LiteApi.IEnvManager");
}

QString GdbDebugeer::mimeType() const
{
    return QLatin1String("debuger/gdb");
}

QAbstractItemModel *GdbDebugeer::debugModel(LiteApi::DEBUG_MODEL_TYPE type)
{
    if (type == LiteApi::EXECUTION_MODEL) {
        return m_executionModel;
    } else if (type == LiteApi::LOCALS_MODEL) {
        return m_localsModel;
    } else if (type == LiteApi::CALLSTACK_MODEL) {
        return m_framesModel;
    }
    return 0;
}

void GdbDebugeer::setWorkingDirectory(const QString &dir)
{
    m_process->setWorkingDirectory(dir);
}

void GdbDebugeer::setEnvironment (const QStringList &environment)
{
    m_process->setEnvironment(environment);
}

bool GdbDebugeer::start(const QString &program, const QStringList &arguments)
{
    if (!m_envManager) {
        return false;
    }

    QStringList args;
    args << "--interpreter=mi";

    QString goroot = m_envManager->currentEnvironment().value("GOROOT");
    if (!goroot.isEmpty()) {
        QString path = QFileInfo(QDir(goroot),"src/pkg/runtime/").path();
        args << "--directory" << m_runtime;
        m_runtime = path.toUtf8();
    }

    args << "--args" << program;
    if (!arguments.isEmpty()) {
        args << arguments;
    }

    m_cmd = FileUtil::lookPath("gdb",m_envManager->currentEnvironment(),true);
    if (m_cmd.isEmpty()) {
        return false;
    }

    m_gdbinit = false;
    m_busy = false;
    m_index = 0;
    m_process->start(m_cmd,args);

    return true;
}

void GdbDebugeer::stop()
{
    appendCmd("-gdb-exit",true);
}

bool GdbDebugeer::isDebugging()
{
    return m_process->state() != QProcess::NotRunning;
}

void GdbDebugeer::abort()
{
    m_process->kill();
}

void GdbDebugeer::stepOver()
{
    appendCmd("-exec-next",true);
}

void GdbDebugeer::stepInto()
{
    appendCmd("-exec-step",true);
}

void GdbDebugeer::stepOut()
{
    appendCmd("-exec-finish",true);
}

void GdbDebugeer::appendCmd(const QByteArray &cmd, bool exec)
{
    m_gdbCommand.append(cmd);
    writeCmd();
}

void GdbDebugeer::writeCmd()
{
    if (!m_gdbCommand.isEmpty()) {
        QString cmd = m_gdbCommand.takeFirst();
        QString num = QString::number(m_index++);
        QString c = QString("%1%2\r\n").arg(num,8,'0').arg(cmd);
        m_process->write(c.toLatin1());
    }
}

void GdbDebugeer::readStdError()
{
    qDebug() << "err" << m_process->readAllStandardError();
}

/*
27.4.2 gdb/mi Output Syntax

The output from gdb/mi consists of zero or more out-of-band records followed, optionally,
by a single result record. This result record is for the most recent command. The sequence
of output records is terminated by ��(gdb)��.
If an input command was prefixed with a token then the corresponding output for that
command will also be prefixed by that same token.

If an input command was prefixed with a token then the corresponding output for that
command will also be prefixed by that same token.

output -> ( out-of-band-record )* [ result-record ] "(gdb)" nl
result-record ->
[ token ] "^" result-class ( "," result )* nl
out-of-band-record ->
async-record | stream-record
async-record ->
exec-async-output | status-async-output | notify-async-output
exec-async-output ->
[ token ] "*" async-output
status-async-output ->
[ token ] "+" async-output
notify-async-output ->
[ token ] "=" async-output
async-output ->
async-class ( "," result )* nl
result-class ->
"done" | "running" | "connected" | "error" | "exit"
async-class ->
"stopped" | others (where others will be added depending on the needs��this
is still in development).
result -> variable "=" value
variable ->
string
value -> const | tuple | list
const -> c-string
tuple -> "{}" | "{" result ( "," result )* "}"
list -> "[]" | "[" value ( "," value )* "]" | "[" result ( "," result )* "]"
stream-record ->
console-stream-output | target-stream-output | log-stream-output
console-stream-output ->
"~" c-string
target-stream-output ->
"@" c-string
log-stream-output ->
"&" c-string
nl -> CR | CR-LF
*/

static bool isNameChar(char c)
{
    // could be 'stopped' or 'shlibs-added'
    return (c >= 'a' && c <= 'z') || c == '-';
}

void GdbDebugeer::handleResponse(const QByteArray &buff)
{
    if (buff.isEmpty() || buff == "(gdb) ")
        return;

    const char *from = buff.constData();
    const char *to = from + buff.size();
    const char *inner;

    int token = -1;
    // Token is a sequence of numbers.
    for (inner = from; inner != to; ++inner)
        if (*inner < '0' || *inner > '9')
            break;
    if (from != inner) {
        token = QByteArray(from, inner - from).toInt();
        from = inner;
    }
    // Next char decides kind of response.
    const char c = *from++;
    switch (c) {
    case '*':
    case '+':
    case '=':
    {
        QByteArray asyncClass;
        for (; from != to; ++from) {
            const char c = *from;
            if (!isNameChar(c))
                break;
            asyncClass += *from;
        }
        GdbMiValue result;
        while (from != to) {
            GdbMiValue data;
            if (*from != ',') {
                // happens on archer where we get
                // 23^running <NL> *running,thread-id="all" <NL> (gdb)
                result.m_type = GdbMiValue::Tuple;
                break;
            }
            ++from; // skip ','
            data.parseResultOrValue(from, to);
            if (data.isValid()) {
                //qDebug() << "parsed result:" << data.toString();
                result.m_children += data;
                result.m_type = GdbMiValue::Tuple;
            }
        }
        handleAsyncClass(asyncClass,result);
        break;
    }
    case '~':
        handleConsoleStream(GdbMiValue::parseCString(from, to));
        break;
    case '@':
        handleTargetStream(GdbMiValue::parseCString(from, to));
        break;
    case '&':
        handleLogStream(GdbMiValue::parseCString(from, to));
        break;
    case '^': {
        GdbResponse response;

        response.token = token;

        for (inner = from; inner != to; ++inner)
            if (*inner < 'a' || *inner > 'z')
                break;

        QByteArray resultClass = QByteArray::fromRawData(from, inner - from);
        if (resultClass == "done") {
            response.resultClass = GdbResultDone;
        } else if (resultClass == "running") {
            response.resultClass = GdbResultRunning;
        } else if (resultClass == "connected") {
            response.resultClass = GdbResultConnected;
        } else if (resultClass == "error") {
            response.resultClass = GdbResultError;
        } else if (resultClass == "exit") {
            response.resultClass = GdbResultExit;
        } else {
            response.resultClass = GdbResultUnknown;
        }

        from = inner;
        if (from != to) {
            if (*from == ',') {
                ++from;
                response.data.parseTuple_helper(from, to);
                response.data.m_type = GdbMiValue::Tuple;
                response.data.m_name = "data";
            } else {
                // Archer has this.
                response.data.m_type = GdbMiValue::Tuple;
                response.data.m_name = "data";
            }
        }
        handleResultRecord(response);
        break;
    }
    default: {
        qDebug() << "UNKNOWN RESPONSE TYPE" << c;
        break;
    }
    }
}

void GdbDebugeer::handleStopped(const GdbMiValue &result)
{
    const QByteArray reason = result.findChild("reason").data();
    m_handleState.setStopped(true);
    if (reason.startsWith("exited")) {
        m_handleState.setExited(true);
        m_handleState.appendMsg(reason);
        return;
    }
}

void GdbDebugeer::handleAsyncClass(const QByteArray &asyncClass, const GdbMiValue &result)
{
    if (asyncClass == "stopped") {
        handleStopped(result);
    }
    QString thread_id = result.findChild("thread-id").data();
    QString stopped_threads = result.findChild("stopped-threads").data();
    GdbMiValue frame = result.findChild("frame");
    if (frame.isValid()) {
        QString addr = frame.findChild("addr").data();
        QString func = frame.findChild("func").data();
        QString file = frame.findChild("file").data();
        QString fullname = frame.findChild("fullname").data();
        QString line = frame.findChild("line").data();
        QList<QStandardItem*> items;
        items << new QStandardItem(addr)
              << new QStandardItem(func)
              << new QStandardItem(file)
              << new QStandardItem(line)
              << new QStandardItem(thread_id);
        m_executionModel->removeRows(0,m_executionModel->rowCount());
        m_executionModel->appendRow(items);
        qDebug() << fullname;
        if (QFile::exists(fullname)) {
            LiteApi::IEditor *editor = m_liteApp->fileManager()->openEditor(fullname,true);
            if (editor) {
                bool ok = false;
                int n = line.toInt(&ok);
                if (ok) {
                    LiteApi::ITextEditor *textEditor = LiteApi::findExtensionObject<LiteApi::ITextEditor*>(editor,"LiteApi.ITextEditor");
                    if (textEditor) {
                        textEditor->gotoLine(n,0);
                    }
                }
            }
        }
    }
}

void GdbDebugeer::handleConsoleStream(const QByteArray &data)
{

}

void GdbDebugeer::handleTargetStream(const QByteArray &data)
{

}

void GdbDebugeer::handleLogStream(const QByteArray &data)
{

}

void GdbDebugeer::handleResultRecord(const GdbResponse &response)
{
    GdbMiValue locals = response.data.findChild("locals");
    if (locals.isValid() && locals.isList()) {
        m_localsModel->removeRows(0,m_localsModel->rowCount());
        for (int i = 0; i < locals.childCount(); i++) {
            GdbMiValue child = locals.childAt(i);
            if (child.isValid()) {
                QString name = child.findChild("name").data();
                QString type = child.findChild("type").data();
                QString value = child.findChild("value").data();
                m_localsModel->appendRow(QList<QStandardItem*>()
                                         << new QStandardItem(name)
                                         << new QStandardItem(type)
                                         << new QStandardItem(value) );
            }
        }
        return;
    }
    GdbMiValue stack = response.data.findChild("stack");
    if (stack.isValid() && stack.isList()) {
        m_framesModel->removeRows(0,m_framesModel->rowCount());
        for (int i = 0; i < stack.childCount(); i++) {
            GdbMiValue child = stack.childAt(i);
            if (child.isValid() && child.name() == "frame") {
                QString level = child.findChild("level").data();
                QString addr = child.findChild("addr").data();
                QString func = child.findChild("func").data();
                QString file = child.findChild("file").data();
                QString line = child.findChild("line").data();
                m_framesModel->appendRow(QList<QStandardItem*>()
                                         << new QStandardItem(level)
                                         << new QStandardItem(addr)
                                         << new QStandardItem(func)
                                         << new QStandardItem(file)
                                         << new QStandardItem(line)
                                         );
            }
        }
    }
}


void GdbDebugeer::initGdb()
{
#ifdef Q_OS_WIN
    appendCmd("set new-console on");
#endif
    appendCmd("set unwindonsignal on");
    appendCmd("set overload-resolution off");
    appendCmd("handle SIGSEGV nopass stop print");
    appendCmd("set breakpoint pending on");
    appendCmd("set width 0");
    appendCmd("set height 0");
    appendCmd("set auto-solib-add on");
    if (!m_runtime.isEmpty()) {
        appendCmd("set substitute-path /go/src/pkg/runtime "+m_runtime);
    }
    appendCmd("break main.main");
    appendCmd("-exec-run");
}

void GdbDebugeer::updateLocals()
{
    appendCmd("-stack-list-locals 2");
}

void  GdbDebugeer::updateFrames()
{
    appendCmd("-stack-list-frames");
}

void GdbDebugeer::readStdOutput()
{
    int newstart = 0;
    int scan = m_inbuffer.size();
    m_inbuffer.append(m_process->readAllStandardOutput());

    // This can trigger when a dialog starts a nested event loop.
    if (m_busy)
        return;

    while (newstart < m_inbuffer.size()) {
        int start = newstart;
        int end = m_inbuffer.indexOf('\n', scan);
        if (end < 0) {
            m_inbuffer.remove(0, start);
            return;
        }
        newstart = end + 1;
        scan = newstart;
        if (end == start)
            continue;
#ifdef Q_OS_WIN
        if (m_inbuffer.at(end - 1) == '\r') {
            --end;
            if (end == start)
                continue;
        }
#endif
        m_busy = true;
        QByteArray data = QByteArray::fromRawData(m_inbuffer.constData() + start, end - start);
        handleResponse(data);
        m_busy = false;
    }
    m_inbuffer.clear();

    if (m_handleState.exited()) {
        stop();
    }

    writeCmd();

    if (!m_gdbinit) {
        m_gdbinit = true;
        initGdb();
    }

    if (m_handleState.stopped()) {
        //get locals
        updateLocals();
        updateFrames();
    }
    m_handleState.clear();
}