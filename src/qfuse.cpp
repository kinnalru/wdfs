
#include <error.h>
#include <QFile>
#include <QTextStream>
#include <QString>
#include <QDir>
#include <QDebug>
#include <QCoreApplication>
#include <QMetaObject>
#include <QTimer>

#include <iostream>

#include <string.h>

#include "qfuse.h"
// #include <fuse/fuse_lowlevel.h>


static struct fuse_server {
    pthread_t pid;
    struct fuse *fuse;
    struct fuse_chan *ch;
    int failed;
    int running;
} fs;



static void *fuse_thread(void *arg)
{
    if(arg) {}

//     if (fs.multithreaded) {
//         std::cout << __FUNCTION__ << ": fuse_loop_mt()" << std::endl;
//         if (fuse_loop_mt(fs.fuse) < 0) {
//             perror("problem in fuse_loop_mt");
//             fs.failed = 1;
//         }
//         std::cout << __FUNCTION__ << " exiting fuse_loop() now"<< std::endl;
//     } else {
        std::cout << __FUNCTION__ << ": fuse_loop()"<< std::endl;
        if (fuse_loop(fs.fuse) < 0) {
            perror("problem in fuse_loop");
            fs.failed = 1;
        }
        std::cout << __FUNCTION__ << " exiting fuse_loop_mt() now" << std::endl;
//     }

    // this call will shutdown the qcoreapplication via the qfuse object
    QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::QueuedConnection);

    return NULL;
}


QFuse::QFuse(struct fuse_operations* fo, struct fuse_args* fa, QObject* parent)
    : QObject(parent)
    , fo_(fo)
    , fa_(fa)
    , ch_(NULL)
    , fuse_(NULL)
{}

void QFuse::init()
{
char *mountpoint;
        int multithreaded = 0;

        struct fuse_args args = *fa_;
        struct fuse *fuse;
        int foreground;
        int res;

        qDebug() << 1 ;
        res = fuse_parse_cmdline(&args, &mountpoint, &multithreaded, &foreground);
        
//         qDebug() << 2 ;
        if (res == -1) {
                 qDebug() << "err 0";
        }

        qDebug() << 3;
        ch_ = fuse_mount( mountpoint, &args);
        qDebug() << 4;
        if (!ch_) {
                fuse_opt_free_args(&args);
//                 goto err_free;
        }

        fuse = fuse_new(ch_, &args, fo_, sizeof(struct fuse_operations), 0);
        fuse_opt_free_args(&args);
        if (fuse == NULL) {
//                 goto err_unmount;
            qDebug() << "err 1";
        }
        
        fuse_ = fuse;

        res = fuse_daemonize(foreground);
        if (res == -1) {
//                 goto err_unmount;
            qDebug() << "err 2";
        }

//         res = fuse_set_signal_handlers(fuse_get_session(fuse));
//         if (res == -1) {
// //                 goto err_unmount;
//             qDebug() << "err 3";
//         }
    
//     qDebug() << Q_FUNC_INFO << "1";
//     ch_ = fuse_mount("/tmp/fs/", fa_);
//     
//     qDebug() << Q_FUNC_INFO << "2";
//     if(!ch_) {
//         qFatal("fuse_mount error");
//     }
//     
//     qDebug() << Q_FUNC_INFO << "3: "<< sizeof(struct fuse_operations);
//     fuse_ = fuse_new(ch_, fa_, fo_, sizeof(struct fuse_operations), NULL);
//     
//     qDebug() << Q_FUNC_INFO << "4";
//     if(fuse_) {
// //         fuse_unmount("/tmp/fs/", fs.ch);
//         qFatal("fuse_new error");
//     }
    
    qDebug() << Q_FUNC_INFO << "5";
    QFuseLoop* loop = new QFuseLoop(fuse_get_session(fuse_));
    qDebug() << Q_FUNC_INFO << "6";
    
    loop->moveToThread(&fuse_th_);
    connect(&fuse_th_, SIGNAL(finished()), loop, SLOT(deleteLater()));
    connect(&fuse_th_, SIGNAL(started()), loop, SLOT(exec()));
    connect(loop, SIGNAL(processBuf(fuse_buf*,fuse_chan*)), this, SLOT(processBuf(fuse_buf*,fuse_chan*)));
    fuse_th_.start();
    
    qDebug() << Q_FUNC_INFO << "7";
    perror(strerror(errno));
}


QFuse::~QFuse() {
//     qDebug().nospace()   << __FUNCTION__  ;
}

void QFuse::processBuf(fuse_buf* fbuf, fuse_chan* ch)
{
     qDebug() << Q_FUNC_INFO;
     
     fuse_session_process_buf(fuse_get_session(fuse_), fbuf, ch);
}



// int QFuse::shutDown() {
//     // this must be called only once
//     static int guard = 0;
// 
//     if (guard == 1) {
//         qDebug().nospace()   << __FUNCTION__ << " already called, won't be executed twice"  ;
//         return 0;
//     }
// 
//     guard=1;
// 
//     if (fs.running) {
//         qDebug().nospace()   << __FUNCTION__ << " fuse_session_exit"  ;
//         fuse_session_exit (fuse_get_session(fs.fuse));
// 
//         qDebug().nospace()   << __FUNCTION__ << " fuse_unmount()"  ;
//         fuse_unmount("/tmp/fs/", fs.ch);
// 
//         qDebug().nospace()   << __FUNCTION__ << " calling pthread_join()"  ;
//         pthread_join(fs.pid, NULL);
// 
//         fs.fuse = NULL;
//         memset(&fs, 0, sizeof(fs));
//     }
// 
//     qDebug().nospace()   << __FUNCTION__ << " emit sigShutDownComplete()"  ;
//     emit sigShutDownComplete();
// 
//     return 0;
// }









