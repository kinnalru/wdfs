

#include <QDebug>

#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <unistd.h>

#include "qsighandler.h"

int QSigHandler::sigtermFd[2];
int QSigHandler::sigintFd[2];

QSigHandler::QSigHandler(QObject *parent)
    : QObject(parent)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigtermFd))
        qFatal("Couldn't create TERM socketpair");

    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigintFd))
        qFatal("Couldn't create INT socketpair");

    snInt = new QSocketNotifier(sigintFd[1], QSocketNotifier::Read, this);
    connect(snInt, SIGNAL(activated(int)), this, SLOT(handleSigint()));

    snTerm = new QSocketNotifier(sigtermFd[1], QSocketNotifier::Read, this);
    connect(snTerm, SIGNAL(activated(int)), this, SLOT(handleSigterm()));
}


QSigHandler::~QSigHandler() {
}

void QSigHandler::sigintHandler(int)
{
    char a = 1;
    if (::write(sigintFd[0], &a, sizeof(a)) != 0) {}
}

void QSigHandler::sigtermHandler(int)
{
    char a = 1;
    if (::write(sigtermFd[0], &a, sizeof(a))) {}
}

void QSigHandler::handleSigint()
{
    snInt->setEnabled(false);
    char tmp;
    if (::read(sigintFd[1], &tmp, sizeof(tmp))) {}

    qDebug() << "Sigint !";
    
    Q_EMIT sigint();
    snInt->setEnabled(true);
}

void QSigHandler::handleSigterm()
{
    snTerm->setEnabled(false);
    char tmp;
    if (::read(sigtermFd[1], &tmp, sizeof(tmp))) {}

    qDebug() << "Sigterm !";
    
    Q_EMIT sigterm();
    snTerm->setEnabled(true);
}
