#include <QtCore/QDebug>
#include <QtCore/QTimer>

#include <QtDBus/QtDBus>

#include <TelepathyQt4/Constants>
#include <TelepathyQt4/Client/DBus>
#include <TelepathyQt4/Client/Channel>
#include <TelepathyQt4/Client/Connection>
#include <TelepathyQt4/Client/ConnectionManager>
#include <TelepathyQt4/Client/PendingChannel>
#include <TelepathyQt4/Client/PendingHandles>

#include <tests/pinocchio/lib.h>

using namespace Telepathy::Client;

class TestChanBasics : public PinocchioTest
{
    Q_OBJECT

private:
    ConnectionManagerInterface* mCM;

    QString mConnBusName;
    QString mConnObjectPath;
    Connection *mConn;

    uint mSubscribeHandle;
    QString mSubscribeChanObjectPath;
    Channel *mChan;

protected Q_SLOTS:
    // these ought to be private, but if they were, QTest would think they
    // were test cases. So, they're protected instead
    void expectConnReady(uint, uint);
    void expectChanReady(uint);
    void expectPendingChannelFinished(Telepathy::Client::PendingOperation*);
    void expectPendingChannelError(Telepathy::Client::PendingOperation*);
    void expectRequestHandlesFinished(Telepathy::Client::PendingOperation*);
    void expectRequestChannelFinished(Telepathy::Client::PendingOperation*);

private Q_SLOTS:
    void initTestCase();
    void init();

    void testBasics();
    void testPendingChannel();
    void testPendingChannelError();

    void cleanup();
    void cleanupTestCase();
};


void TestChanBasics::expectConnReady(uint newStatus, uint newStatusReason)
{
    switch (newStatus) {
    case Telepathy::ConnectionStatusDisconnected:
        qWarning() << "Disconnected";
        mLoop->exit(1);
        break;
    case Telepathy::ConnectionStatusConnecting:
        /* do nothing */
        break;
    case Telepathy::ConnectionStatusConnected:
        qDebug() << "Ready";
        mLoop->exit(0);
        break;
    default:
        qWarning().nospace() << "What sort of status is "
            << newStatus << "?!";
        mLoop->exit(2);
        break;
    }
}


void TestChanBasics::initTestCase()
{
    initTestCaseImpl();

    // Wait for the CM to start up
    QVERIFY(waitForPinocchio(5000));

    // Escape to the low-level API to make a Connection; this uses
    // pseudo-blocking calls for simplicity. Do not do this in production code

    mCM = new Telepathy::Client::ConnectionManagerInterface(
        pinocchioBusName(), pinocchioObjectPath());

    QDBusPendingReply<QString, QDBusObjectPath> reply;
    QVariantMap parameters;
    parameters.insert(QLatin1String("account"),
        QVariant::fromValue(QString::fromAscii("empty")));
    parameters.insert(QLatin1String("password"),
        QVariant::fromValue(QString::fromAscii("s3kr1t")));

    reply = mCM->RequestConnection("dummy", parameters);
    reply.waitForFinished();
    if (!reply.isValid()) {
        qWarning().nospace() << reply.error().name()
            << ": " << reply.error().message();
        QVERIFY(reply.isValid());
    }
    mConnBusName = reply.argumentAt<0>();
    mConnObjectPath = reply.argumentAt<1>().path();

    // Get a connected Connection
    mConn = new Connection(mConnBusName, mConnObjectPath);

    qDebug() << "calling Connect()";
    QVERIFY(connect(mConn->requestConnect(),
            SIGNAL(finished(Telepathy::Client::PendingOperation*)),
            this,
            SLOT(expectSuccessfulCall(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);

    QVERIFY(connect(mConn, SIGNAL(statusChanged(uint, uint)),
          this, SLOT(expectConnReady(uint, uint))));
    QCOMPARE(mLoop->exec(), 0);
    QVERIFY(disconnect(mConn, SIGNAL(statusChanged(uint, uint)),
          this, SLOT(expectConnReady(uint, uint))));

    QVERIFY(connect(mConn->requestHandles(Telepathy::HandleTypeList,
                                          QStringList() << "subscribe"),
                    SIGNAL(finished(Telepathy::Client::PendingOperation*)),
                    this,
                    SLOT(expectRequestHandlesFinished(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);

    connect(mConn->requestChannel(TELEPATHY_INTERFACE_CHANNEL_TYPE_CONTACT_LIST,
                                  Telepathy::HandleTypeList, mSubscribeHandle),
            SIGNAL(finished(Telepathy::Client::PendingOperation*)),
            this,
            SLOT(expectRequestChannelFinished(Telepathy::Client::PendingOperation*)));
    QCOMPARE(mLoop->exec(), 0);
}


void TestChanBasics::init()
{
    initImpl();
}


void TestChanBasics::expectChanReady(uint newReadiness)
{
    switch (newReadiness) {
    case Channel::ReadinessJustCreated:
        qWarning() << "Changing from JustCreated to JustCreated is silly";
        mLoop->exit(1);
        break;
    case Channel::ReadinessFull:
        qDebug() << "Ready";
        mLoop->exit(0);
        break;
    case Channel::ReadinessClosed:
        // fall through
    case Channel::ReadinessDead:
        qWarning() << "Dead or closed!";
        mLoop->exit(3);
        break;
    default:
        qWarning().nospace() << "What sort of readiness is "
            << newReadiness << "?!";
        mLoop->exit(4);
        break;
    }
}


void TestChanBasics::testBasics()
{
    mChan = new Channel(mConn, mSubscribeChanObjectPath);

    QCOMPARE(mChan->readiness(), Channel::ReadinessJustCreated);
    QEXPECT_FAIL("", "Doesn't seem to work", Continue);
    QCOMPARE(mChan->connection(), mConn);

    // Wait for readiness to reach Full

    qDebug() << "waiting for Full readiness";
    QVERIFY(connect(mChan, SIGNAL(readinessChanged(uint)),
          this, SLOT(expectChanReady(uint))));
    QCOMPARE(mLoop->exec(), 0);
    QVERIFY(disconnect(mChan, SIGNAL(readinessChanged(uint)),
          this, SLOT(expectChanReady(uint))));

    QCOMPARE(mChan->readiness(), Channel::ReadinessFull);

    QCOMPARE(mChan->channelType(),
        QString::fromAscii(TELEPATHY_INTERFACE_CHANNEL_TYPE_CONTACT_LIST));
    QCOMPARE(mChan->targetHandleType(),
        static_cast<uint>(Telepathy::HandleTypeList));
    QCOMPARE(mChan->targetHandle(), mSubscribeHandle);

    delete mChan;
}


void TestChanBasics::expectPendingChannelFinished(PendingOperation* op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (op->isError()) {
        qWarning().nospace() << op->errorName()
            << ": " << op->errorMessage();
        mLoop->exit(2);
        return;
    }

    if (!op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    PendingChannel *pc = qobject_cast<PendingChannel*>(op);
    mChan = pc->channel();
    mLoop->exit(0);
}

void TestChanBasics::expectRequestHandlesFinished(PendingOperation* op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (op->isError()) {
        qWarning().nospace() << op->errorName()
            << ": " << op->errorMessage();
        mLoop->exit(2);
        return;
    }

    if (!op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    PendingHandles *ph = qobject_cast<PendingHandles*>(op);
    mSubscribeHandle = ph->handles().at(0);
    mLoop->exit(0);
}

void TestChanBasics::expectRequestChannelFinished(PendingOperation* op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (op->isError()) {
        qWarning().nospace() << op->errorName()
            << ": " << op->errorMessage();
        mLoop->exit(2);
        return;
    }

    if (!op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    PendingChannel *pc = qobject_cast<PendingChannel*>(op);
    Channel *chan = pc->channel();
    mSubscribeChanObjectPath = chan->objectPath();
    mLoop->exit(0);
}

void TestChanBasics::testPendingChannel()
{
    PendingChannel *pc = mConn->requestChannel(
        QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_CONTACT_LIST),
        Telepathy::HandleTypeList,
        mSubscribeHandle);

    QVERIFY(connect(pc, SIGNAL(finished(Telepathy::Client::PendingOperation*)),
          this, SLOT(expectPendingChannelFinished(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);
    QVERIFY(disconnect(pc, SIGNAL(finished(Telepathy::Client::PendingOperation*)),
          this, SLOT(expectPendingChannelFinished(Telepathy::Client::PendingOperation*))));

    QVERIFY(mChan);

    QCOMPARE(mChan->readiness(), Channel::ReadinessJustCreated);
    QEXPECT_FAIL("", "Doesn't seem to work", Continue);
    QCOMPARE(mChan->connection(), mConn);

    // Wait for readiness to reach Full
    // FIXME: eventually, this should be encapsulated in the PendingChannel

    qDebug() << "waiting for Full readiness";
    QVERIFY(connect(mChan, SIGNAL(readinessChanged(uint)),
          this, SLOT(expectChanReady(uint))));
    QCOMPARE(mLoop->exec(), 0);
    QVERIFY(disconnect(mChan, SIGNAL(readinessChanged(uint)),
          this, SLOT(expectChanReady(uint))));

    QCOMPARE(mChan->readiness(), Channel::ReadinessFull);

    QCOMPARE(mChan->channelType(),
        QString::fromAscii(TELEPATHY_INTERFACE_CHANNEL_TYPE_CONTACT_LIST));
    QCOMPARE(mChan->targetHandleType(),
        static_cast<uint>(Telepathy::HandleTypeList));
    QCOMPARE(mChan->targetHandle(), mSubscribeHandle);

    delete mChan;
}

void TestChanBasics::expectPendingChannelError(PendingOperation* op)
{
    if (!op->isFinished()) {
        qWarning() << "unfinished";
        mLoop->exit(1);
        return;
    }

    if (!op->isError()) {
        qWarning() << "no error";
        mLoop->exit(2);
        return;
    }

    qDebug().nospace() << op->errorName()
        << ": " << op->errorMessage();

    if (op->isValid()) {
        qWarning() << "inconsistent results";
        mLoop->exit(3);
        return;
    }

    mLoop->exit(0);
}


void TestChanBasics::testPendingChannelError()
{
    PendingChannel *pc = mConn->requestChannel(
        QLatin1String(TELEPATHY_INTERFACE_CHANNEL_TYPE_CONTACT_LIST),
        Telepathy::HandleTypeList,
        31337);

    QVERIFY(connect(pc, SIGNAL(finished(Telepathy::Client::PendingOperation*)),
          this, SLOT(expectPendingChannelError(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);
    QVERIFY(disconnect(pc, SIGNAL(finished(Telepathy::Client::PendingOperation*)),
          this, SLOT(expectPendingChannelError(Telepathy::Client::PendingOperation*))));
}


void TestChanBasics::cleanup()
{
    cleanupImpl();
}


void TestChanBasics::cleanupTestCase()
{
    QVERIFY(connect(mConn->requestDisconnect(),
          SIGNAL(finished(Telepathy::Client::PendingOperation*)),
          this,
          SLOT(expectSuccessfulCall(Telepathy::Client::PendingOperation*))));
    QCOMPARE(mLoop->exec(), 0);

    delete mConn;
    mConn = NULL;

    delete mCM;
    mCM = NULL;

    cleanupTestCaseImpl();
}


QTEST_MAIN(TestChanBasics)
#include "_gen/chan-basics.cpp.moc.hpp"
