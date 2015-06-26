#ifndef Q_SIGHANDLER_H
#define Q_SIGHANDLER_H

#include <QSocketNotifier>


class QSigHandler : public QObject
{
    Q_OBJECT

public:
    QSigHandler(QObject *parent = 0);
    ~QSigHandler();

    // Unix signal handlers.
    static void sigintHandler(int unused);
    static void sigtermHandler(int unused);

public slots:
    // Qt signal handlers.
    void handleSigint();
    void handleSigterm();
    
signals:
    void sigterm();
    void sigint();

private:
    static int sigintFd[2];
    static int sigtermFd[2];

    QSocketNotifier *snInt;
    QSocketNotifier *snTerm;

};

#endif //Q_SIGHANDLER_H