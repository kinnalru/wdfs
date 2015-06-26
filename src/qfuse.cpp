
#include <error.h>
#include <string.h>
#include <qt4/QtCore/QThread>
#include <iostream>

#include <QFile>
#include <QTextStream>
#include <QString>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include "qfuse.h"

QFuse::QFuse(struct fuse_operations* fo, struct fuse_args* fa)
    : fo_(fo)
    , fa_(fa)
{
}


QFuse::~QFuse() {
}

void QFuse::start()
{
    qDebug() << Q_FUNC_INFO;
    
//     char *mountpoint;
//     int mt = 0;
//     int fg = 0;
//     
//     if (fuse_parse_cmdline(fa_, &mountpoint, &mt, &fg) == -1) {
//         qFatal("can't init fuse args");
//     }
//     
//     qDebug("mt: %d(forced) fg: %d", mt, fg);
//     
//     fuse_ch_.reset(fuse_mount(mountpoint, fa_), [mountpoint](struct fuse_chan *ch){fuse_unmount(mountpoint, ch);});
// 
//     if (!fuse_ch_) {
//         qFatal("can't create fuse channel");
//     }
// 
//     fuse_.reset(fuse_new(fuse_ch_.get(), fa_, fo_, sizeof(struct fuse_operations), 0), fuse_destroy);
//     if (!fuse_) {
//         qFatal("can't create fuse fs");
//     }

    QFuseLoop* loop = new QFuseLoop(fo_, fa_);
    
    loop->moveToThread(&fuse_th_);
    connect(&fuse_th_, SIGNAL(started()), loop, SLOT(exec()));
    connect(loop, SIGNAL(processBuf(fuse_buf*,fuse_chan*)), this, SLOT(processBuf(fuse_buf*, fuse_chan*)), Qt::BlockingQueuedConnection);
    connect(loop, SIGNAL(finished()), this, SLOT(stop()));
    connect(&fuse_th_, SIGNAL(finished()), loop, SLOT(deleteLater()));
    
    qDebug() << Q_FUNC_INFO << "staring thread....";
    fuse_th_.start();
}

void QFuse::stop()
{
    qDebug() << Q_FUNC_INFO;
    fuse_.reset();
    fuse_ch_.reset();
    fuse_th_.quit();
}



void QFuse::processBuf(fuse_buf* fbuf, fuse_chan* ch)
{
    qDebug() << Q_FUNC_INFO << "process buff th " << QThread::currentThread();
    fuse_session_process_buf(fuse_get_session(fuse_.get()), fbuf, ch);
}








