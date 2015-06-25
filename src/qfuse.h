#ifndef QFUSE_HH
#define QFUSE_HH

#define FUSE_USE_VERSION 29

#include <QDebug>
#include <QObject>
#include <QThread>

#include <fuse.h>
#include <fuse_lowlevel.h>

#include "qfuseloop.h"


class QFuse : public QObject {
    Q_OBJECT
public:
    QFuse(struct fuse_operations* fo, struct fuse_args* fa, QObject* parent = 0);
    ~QFuse();
    
public Q_SLOTS:
    void init();
    
private Q_SLOTS:
    void processBuf(struct fuse_buf* fbuf, struct fuse_chan* ch);
    
private:
    struct fuse_operations* fo_;
    struct fuse_args* fa_;

    std::shared_ptr<struct fuse_chan> fuse_ch_;
    std::shared_ptr<struct fuse> fuse_;
    
    QThread fuse_th_;
    
    int fuse_stat;
    int argc;
    char** argv;
};

#endif
