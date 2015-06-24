
#ifndef Q_FUSE_LOOP_H
#define Q_FUSE_LOOP_H

#define FUSE_USE_VERSION 26

#include <memory>

#include <fuse_lowlevel.h>

#include <QDebug>
#include <QObject>

class QFuseLoop : public QObject {
    Q_OBJECT
    
public:
    QFuseLoop(fuse_session *se) : se_(se) {};
    ~QFuseLoop(){};
    
public Q_SLOTS:
    
    int exec() {
        
        qDebug() << Q_FUNC_INFO << "1";
        int res = 0;
        struct fuse_chan *ch = fuse_session_next_chan(se_, NULL);
        size_t bufsize = fuse_chan_bufsize(ch);
        std::shared_ptr<char> buf((char*)malloc(bufsize), free);
        if (!buf) {
            qCritical() << "fuse: failed to allocate read buffer\n";
            return -1;
        }

        qDebug() << Q_FUNC_INFO << "2";
        while (!fuse_session_exited(se_)) {
            struct fuse_chan *tmpch = ch;
            struct fuse_buf fbuf;
            fbuf.mem = buf.get();
            fbuf.size = bufsize;

            qDebug() << Q_FUNC_INFO << "3";
            res = fuse_session_receive_buf(se_, &fbuf, &tmpch);

            if (res == -EINTR)
                continue;
            if (res <= 0)
                break;
            
            Q_EMIT processBuf(&fbuf, tmpch);
// 
//             fuse_session_process_buf(se_, &fbuf, tmpch);
        }

        fuse_session_reset(se_);
        return res < 0 ? -1 : 0;
    };
    
Q_SIGNALS:
    void processBuf(struct fuse_buf* , struct fuse_chan *);
    
private:
    struct fuse_session *se_;
};



#endif // Q_FUSE_LOOP_H