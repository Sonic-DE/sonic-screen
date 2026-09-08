// SPDX-License-Identifier: GPL-2.0-or-later

#include "kscreenapplet.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QSignalSpy>
#include <QTest>

#include <utility>

using InhibitionInfo = QPair<QString, QString>;

class PolicyAgentMock : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.Solid.PowerManagement.PolicyAgent")

public:
    QList<InhibitionInfo> entries{{QStringLiteral("org.example.Player"), QStringLiteral("Playing video")}};

public Q_SLOTS:
    QList<InhibitionInfo> ListInhibitions() const
    {
        return entries;
    }

Q_SIGNALS:
    void InhibitionsChanged(const QList<InhibitionInfo> &added, const QStringList &removed);
};

class ScreenSaverMock : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.ScreenSaver")

public:
    QString reason;
    uint uninhibitedCookie = 0;
    bool delayInhibit = false;
    QList<QDBusMessage> pendingInhibits;

    void releaseInhibits()
    {
        const auto pending = std::exchange(pendingInhibits, {});
        for (const QDBusMessage &request : pending) {
            QDBusConnection(QStringLiteral("screen-test-service")).send(request.createReply(QVariant::fromValue(42u)));
        }
    }

public Q_SLOTS:
    uint Inhibit(const QString &, const QString &requestedReason)
    {
        reason = requestedReason;
        if (delayInhibit) {
            setDelayedReply(true);
            pendingInhibits.append(message());
            return 0;
        }
        return 42;
    }

    void UnInhibit(uint cookie)
    {
        uninhibitedCookie = cookie;
    }
};

class PresentationModeDbusTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qDBusRegisterMetaType<InhibitionInfo>();
        qDBusRegisterMetaType<QList<InhibitionInfo>>();

        QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("screen-test-service"));
        QVERIFY(bus.registerService(QStringLiteral("org.kde.Solid.PowerManagement")));
        QVERIFY(bus.registerObject(QStringLiteral("/org/kde/Solid/PowerManagement/PolicyAgent"),
                                   &m_policyAgent,
                                   QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals));
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
        QVERIFY(bus.registerObject(QStringLiteral("/ScreenSaver"), &m_screenSaver, QDBusConnection::ExportAllSlots));
    }

    void listsAndTracksInhibitions()
    {
        KScreenApplet applet(nullptr, KPluginMetaData(), {});
        applet.init();

        QTRY_COMPARE_WITH_TIMEOUT(applet.inhibitions().size(), 1, 5000);
        QCOMPARE(applet.inhibitions().constFirst().toMap().value(QStringLiteral("Name")).toString(), QStringLiteral("org.example.Player"));
        QCOMPARE(applet.inhibitions().constFirst().toMap().value(QStringLiteral("Reason")).toString(), QStringLiteral("Playing video"));

        QSignalSpy spy(&applet, &KScreenApplet::inhibitionsChanged);
        Q_EMIT m_policyAgent.InhibitionsChanged({{QStringLiteral("org.example.Recorder"), QStringLiteral("Recording")}}, {});
        QTRY_COMPARE_WITH_TIMEOUT(applet.inhibitions().size(), 2, 5000);
        QVERIFY(!spy.isEmpty());
    }

    void inhibitsAndUninhibitsScreenSaver()
    {
        KScreenApplet applet(nullptr, KPluginMetaData(), {});
        applet.enablePresentationMode(QStringLiteral("User enabled presentation mode"));
        QTRY_VERIFY_WITH_TIMEOUT(!applet.presentationModePending(), 5000);
        QVERIFY(applet.presentationModeEnabled());
        QCOMPARE(m_screenSaver.reason, QStringLiteral("User enabled presentation mode"));

        applet.disablePresentationMode();
        QTRY_VERIFY_WITH_TIMEOUT(!applet.presentationModePending(), 5000);
        QVERIFY(!applet.presentationModeEnabled());
        QCOMPARE(m_screenSaver.uninhibitedCookie, 42u);
    }

    void snapshotReplacementRemovesMissingInhibitions()
    {
        m_policyAgent.entries = {{QStringLiteral("org.example.Player"), QStringLiteral("Playing video")}};
        KScreenApplet applet(nullptr, KPluginMetaData(), {});
        applet.init();
        QTRY_COMPARE_WITH_TIMEOUT(applet.inhibitions().size(), 1, 5000);

        m_policyAgent.entries.clear();
        QDBusConnection bus(QStringLiteral("screen-test-service"));
        QVERIFY(bus.unregisterService(QStringLiteral("org.kde.Solid.PowerManagement")));
        QTRY_COMPARE_WITH_TIMEOUT(applet.inhibitions().size(), 0, 5000);
        QVERIFY(bus.registerService(QStringLiteral("org.kde.Solid.PowerManagement")));
        QTRY_COMPARE_WITH_TIMEOUT(applet.inhibitions().size(), 0, 5000);
    }

    void screenSaverRestartClearsPresentationState()
    {
        KScreenApplet applet(nullptr, KPluginMetaData(), {});
        applet.init();
        applet.enablePresentationMode(QStringLiteral("restart test"));
        QTRY_VERIFY_WITH_TIMEOUT(applet.presentationModeEnabled(), 5000);

        QDBusConnection bus(QStringLiteral("screen-test-service"));
        QVERIFY(bus.unregisterService(QStringLiteral("org.freedesktop.ScreenSaver")));
        QTRY_VERIFY_WITH_TIMEOUT(!applet.presentationModeEnabled(), 5000);
        QVERIFY(!applet.presentationModePending());
        QVERIFY(bus.registerService(QStringLiteral("org.freedesktop.ScreenSaver")));
    }

    void lateCookieIsReleasedAfterAppletDestruction()
    {
        m_screenSaver.delayInhibit = true;
        m_screenSaver.uninhibitedCookie = 0;
        auto *applet = new KScreenApplet(nullptr, KPluginMetaData(), {});
        applet->init();
        applet->enablePresentationMode(QStringLiteral("late cookie"));
        QTRY_COMPARE_WITH_TIMEOUT(m_screenSaver.pendingInhibits.size(), 1, 5000);
        delete applet;

        m_screenSaver.releaseInhibits();
        QTRY_COMPARE_WITH_TIMEOUT(m_screenSaver.uninhibitedCookie, 42u, 5000);
        m_screenSaver.delayInhibit = false;
    }

private:
    PolicyAgentMock m_policyAgent;
    ScreenSaverMock m_screenSaver;
};

QTEST_MAIN(PresentationModeDbusTest)

#include "presentationmodedbustest.moc"
