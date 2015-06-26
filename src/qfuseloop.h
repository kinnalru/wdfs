
#ifndef Q_FUSE_LOOP_H
#define Q_FUSE_LOOP_H

#ifndef FUSE_USE_VERSION
    #define FUSE_USE_VERSION 26
#endif

#include <memory>

#include <fuse.h>
#include <fuse_lowlevel.h>

#include <QDebug>
#include <QObject>
#include <QSocketNotifier>
#include <QLocalSocket>

#include <QThread>

#include <unistd.h>

class QFuseLoop : public QObject {
    Q_OBJECT
    
public:
    QFuseLoop(struct fuse_operations* fo, struct fuse_args* fa)
        : fo_(fo)
        , fa_(fa) {};
        
    ~QFuseLoop(){
        qDebug() << Q_FUNC_INFO;
    };
    
public Q_SLOTS:
    
    void exec() {
        
        
        std::shared_ptr<struct fuse_chan> fuse_ch_;
        std::shared_ptr<struct fuse> fuse_;
        
        char *mountpoint;
        int mt = 0;
        int fg = 0;
        
        qDebug() << Q_FUNC_INFO << "parse";
        
        if (fuse_parse_cmdline(fa_, &mountpoint, &mt, &fg) == -1) {
            qFatal("can't init fuse args");
        }
        
        fuse_ch_.reset(fuse_mount(mountpoint, fa_), [mountpoint](struct fuse_chan *ch){fuse_unmount(mountpoint, ch);});

        if (!fuse_ch_) {
            qFatal("can't create fuse channel");
        }

        
        fuse_.reset(fuse_new(fuse_ch_.get(), fa_, fo_, sizeof(struct fuse_operations), 0), fuse_destroy);
        if (!fuse_) {
            qFatal("can't create fuse fs");
        }


        
        
        qDebug() << Q_FUNC_INFO;
        fuse_se_ = fuse_get_session(fuse_.get());
        struct fuse_chan *ch = fuse_session_next_chan(fuse_se_, NULL);
        size_t bufsize = fuse_chan_bufsize(ch);
        std::shared_ptr<char> buf((char*)malloc(bufsize), free);
        
        if (!buf) {
            qFatal("fuse: failed to allocate read buffer\n");
        }
        
//         int fd = fuse_chan_fd(ch);
//         
//         QSocketNotifier* n = new QSocketNotifier(fd, QSocketNotifier::Read);
//         connect(n, SIGNAL(activated(int)), this, SLOT(read()));
//         
//         return ;

        QLocalSocket socket;
        socket.setSocketDescriptor(fuse_chan_fd(ch), QLocalSocket::ConnectedState, QIODevice::ReadOnly);
        
        while (!fuse_session_exited(fuse_se_)) {
            struct fuse_chan *tmpch = ch;
            struct fuse_buf fbuf;
            fbuf.mem = buf.get();
            fbuf.size = bufsize;

//             qDebug() << Q_FUNC_INFO << " Read th:" << QThread::currentThread();
            
            

//              socket.waitForReadyRead(1);
            
            int res = fuse_session_receive_buf(fuse_se_, &fbuf, &tmpch);
            qDebug() << Q_FUNC_INFO << " res:" << res;

            if (res == -EINTR) {
                qDebug() << Q_FUNC_INFO << " EINTR";
                continue;
            }
            if (res <= 0)
                break;

//             Q_EMIT processBuf(&fbuf, tmpch);
            
            qDebug() << Q_FUNC_INFO << " processBuf";
            fuse_session_process_buf(fuse_get_session(fuse_.get()), &fbuf, tmpch);
            qDebug() << Q_FUNC_INFO << " NEXT";
        }

        fuse_session_reset(fuse_se_);
        
        Q_EMIT finished();
    };
    
    void quit() {
        fuse_session_exit(fuse_se_);
    }
    
public Q_SLOTS:
    void read() {
        qDebug() << Q_FUNC_INFO;
    }
    
Q_SIGNALS:
    void processBuf(struct fuse_buf* , struct fuse_chan *);
    void finished();
    
private:
    struct fuse_operations* fo_;
    struct fuse_args* fa_;
    
    struct fuse* fuse_;
    struct fuse_session *fuse_se_;
};



#endif // Q_FUSE_LOOP_H