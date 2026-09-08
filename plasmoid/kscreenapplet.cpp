/*
    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2018 Kai Uwe Broulik <kde@broulik.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#include "kscreenapplet.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QIcon>
#include <QLoggingCategory>
#include <QMetaEnum>
#include <QPointer>
#include <QQmlEngine>

#include <KService>

#include <KScreen/Config>
#include <KScreen/ConfigMonitor>
#include <KScreen/GetConfigOperation>
#include <KScreen/Mode>
#include <KScreen/Output>
#include <KScreen/SetConfigOperation>

#include <algorithm>

Q_LOGGING_CATEGORY(LOG_KSCREEN_APPLET, "org.kde.plasma.kscreen")

// PolicyAgent DBus types
using InhibitionInfo = QPair<QString, QString>;
Q_DECLARE_METATYPE(InhibitionInfo)
Q_DECLARE_METATYPE(QList<InhibitionInfo>)

static const QString s_powerService = QStringLiteral("org.kde.Solid.PowerManagement");
static const QString s_policyAgentPath = QStringLiteral("/org/kde/Solid/PowerManagement/PolicyAgent");
static const QString s_policyAgentInterface = QStringLiteral("org.kde.Solid.PowerManagement.PolicyAgent");

static const QString s_screenSaverService = QStringLiteral("org.freedesktop.ScreenSaver");
static const QString s_screenSaverPath = QStringLiteral("/ScreenSaver");
static const QString s_screenSaverInterface = QStringLiteral("org.freedesktop.ScreenSaver");

K_PLUGIN_CLASS_WITH_JSON(KScreenApplet, "metadata.json")

KScreenApplet::KScreenApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args)
    : Plasma::Applet(parent, data, args)
{
    qRegisterMetaType<InhibitionInfo>();
    qRegisterMetaType<QList<InhibitionInfo>>();
    qDBusRegisterMetaType<InhibitionInfo>();
    qDBusRegisterMetaType<QList<InhibitionInfo>>();
}

KScreenApplet::~KScreenApplet()
{
    if (m_cookie.has_value()) {
        releaseCookie(m_cookieOwner, m_cookie.value());
    }
}

void KScreenApplet::init()
{
    // Presentation mode / inhibition DBus setup
    auto bus = QDBusConnection::sessionBus();
    m_powerOwner = bus.interface()->serviceOwner(s_powerService).value();
    m_screenSaverOwner = bus.interface()->serviceOwner(s_screenSaverService).value();

    m_powerWatcher = new QDBusServiceWatcher(s_powerService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_powerWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &KScreenApplet::onPowerServiceOwnerChanged);
    m_screenSaverWatcher = new QDBusServiceWatcher(s_screenSaverService, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(m_screenSaverWatcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &KScreenApplet::onScreenSaverOwnerChanged);

    queryInhibitions();

    // Subscribe to InhibitionsChanged
    bus.connect(s_powerService,
                s_policyAgentPath,
                s_policyAgentInterface,
                QStringLiteral("InhibitionsChanged"),
                this,
                SLOT(onInhibitionsChanged(QList<QPair<QString, QString>>, QStringList)));

    connect(new KScreen::GetConfigOperation(KScreen::GetConfigOperation::NoEDID),
            &KScreen::ConfigOperation::finished,
            this,
            [this](KScreen::ConfigOperation *op) {
                m_screenConfiguration = qobject_cast<KScreen::GetConfigOperation *>(op)->config();

                KScreen::ConfigMonitor::instance()->addConfig(m_screenConfiguration);
                connect(KScreen::ConfigMonitor::instance(), &KScreen::ConfigMonitor::configurationChanged, this, &KScreenApplet::checkOutputs);

                checkOutputs();
            });
}

int KScreenApplet::connectedOutputCount() const
{
    return m_connectedOutputCount;
}

void KScreenApplet::applyLayoutPreset(KScreen::OsdAction::Action action)
{
    KScreen::OsdAction::applyAction(m_screenConfiguration, action);
}

QVariantList KScreenApplet::inhibitions() const
{
    return m_inhibitionList;
}

bool KScreenApplet::presentationModeEnabled() const
{
    return m_cookie.has_value();
}

bool KScreenApplet::presentationModePending() const
{
    return m_presentationModePending;
}

void KScreenApplet::enablePresentationMode(const QString &reason)
{
    if (m_presentationModePending || m_cookie.has_value()) {
        return;
    }

    const QString owner = QDBusConnection::sessionBus().interface()->serviceOwner(s_screenSaverService).value();
    if (owner.isEmpty()) {
        Q_EMIT presentationModeOperationFailed(QStringLiteral("enable"),
                                               QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown"),
                                               QStringLiteral("ScreenSaver service is unavailable"));
        return;
    }

    setPresentationModePending(true);
    m_screenSaverOwner = owner;
    const quint64 generation = m_screenSaverGeneration;
    const quint64 requestSerial = ++m_presentationRequestSerial;

    QDBusMessage msg = QDBusMessage::createMethodCall(owner, s_screenSaverPath, s_screenSaverInterface, QStringLiteral("Inhibit"));
    msg << QCoreApplication::applicationName() << reason;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), QCoreApplication::instance());
    QPointer<KScreenApplet> self(this);
    connect(watcher, &QDBusPendingCallWatcher::finished, QCoreApplication::instance(), [self, owner, generation, requestSerial](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<uint> reply(*w);
        w->deleteLater();

        if (!self || generation != self->m_screenSaverGeneration || requestSerial != self->m_presentationRequestSerial || owner != self->m_screenSaverOwner) {
            if (!reply.isError()) {
                KScreenApplet::releaseCookie(owner, reply.value());
            }
            return;
        }

        if (reply.isError()) {
            qCWarning(LOG_KSCREEN_APPLET).nospace().noquote()
                << "Inhibit failed: " << s_screenSaverService << " " << s_screenSaverPath << ": " << reply.error().name() << ": " << reply.error().message();
            self->setPresentationModePending(false);
            Q_EMIT self->presentationModeOperationFailed(QStringLiteral("enable"), reply.error().name(), reply.error().message());
        } else {
            self->m_cookie = reply.value();
            self->m_cookieOwner = owner;
            self->setPresentationModePending(false);
            Q_EMIT self->presentationModeEnabledChanged();
        }
    });
}

void KScreenApplet::disablePresentationMode()
{
    if (m_presentationModePending || !m_cookie.has_value()) {
        return;
    }

    setPresentationModePending(true);
    const QString owner = m_cookieOwner;
    const uint cookie = m_cookie.value();
    const quint64 generation = m_screenSaverGeneration;
    const quint64 requestSerial = ++m_presentationRequestSerial;

    QDBusMessage msg = QDBusMessage::createMethodCall(owner, s_screenSaverPath, s_screenSaverInterface, QStringLiteral("UnInhibit"));
    msg << cookie;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, owner, cookie, generation, requestSerial](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<void> reply(*w);
        w->deleteLater();

        if (generation != m_screenSaverGeneration || requestSerial != m_presentationRequestSerial || owner != m_cookieOwner || !m_cookie.has_value()
            || cookie != m_cookie.value()) {
            return;
        }

        if (reply.isError()) {
            qCWarning(LOG_KSCREEN_APPLET).nospace().noquote()
                << "UnInhibit failed: " << s_screenSaverService << " " << s_screenSaverPath << ": " << reply.error().name() << ": " << reply.error().message();
            setPresentationModePending(false);
            Q_EMIT presentationModeOperationFailed(QStringLiteral("disable"), reply.error().name(), reply.error().message());
        } else {
            m_cookie.reset();
            m_cookieOwner.clear();
            setPresentationModePending(false);
            Q_EMIT presentationModeEnabledChanged();
        }
    });
}

void KScreenApplet::releaseCookie(const QString &owner, uint cookie)
{
    if (owner.isEmpty()) {
        return;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(owner, s_screenSaverPath, s_screenSaverInterface, QStringLiteral("UnInhibit"));
    msg << cookie;
    QDBusConnection::sessionBus().asyncCall(msg);
}

void KScreenApplet::checkOutputs()
{
    if (!m_screenConfiguration) {
        return;
    }

    const int oldConnectedOutputCount = m_connectedOutputCount;

    const auto outputs = m_screenConfiguration->outputs();
    m_connectedOutputCount = std::count_if(outputs.begin(), outputs.end(), [](const KScreen::OutputPtr &output) {
        return output->isConnected();
    });

    if (m_connectedOutputCount != oldConnectedOutputCount) {
        emit connectedOutputCountChanged();
    }
}

void KScreenApplet::queryInhibitions()
{
    const QString owner = QDBusConnection::sessionBus().interface()->serviceOwner(s_powerService).value();
    if (owner.isEmpty()) {
        m_inhibitionMap.clear();
        rebuildInhibitions();
        return;
    }
    m_powerOwner = owner;
    const quint64 generation = m_policyGeneration;
    const quint64 revision = m_policyRevision;
    const quint64 serial = ++m_policyQuerySerial;

    QDBusMessage msg = QDBusMessage::createMethodCall(owner, s_policyAgentPath, s_policyAgentInterface, QStringLiteral("ListInhibitions"));

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(msg), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, owner, generation, revision, serial](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QList<InhibitionInfo>> reply(*w);
        w->deleteLater();
        if (generation != m_policyGeneration || serial != m_policyQuerySerial || owner != m_powerOwner) {
            return;
        }
        if (revision != m_policyRevision) {
            queryInhibitions();
            return;
        }
        if (reply.isError()) {
            qCWarning(LOG_KSCREEN_APPLET).nospace().noquote()
                << "ListInhibitions failed: " << s_powerService << " " << s_policyAgentPath << ": " << reply.error().name() << ": " << reply.error().message();
        } else {
            QMap<QString, QVariantMap> snapshot;
            const auto inhibitions = reply.value();
            for (const auto &info : inhibitions) {
                const QString &appId = info.first;
                const QString &reason = info.second;
                if (appId == QStringLiteral("plasmashell") || appId == QStringLiteral("plasmoidviewer")) {
                    continue;
                }
                QVariantMap entry;
                KService::Ptr service = KService::serviceByStorageId(appId + QStringLiteral(".desktop"));
                if (service) {
                    entry[QStringLiteral("Name")] = service->name();
                    entry[QStringLiteral("Icon")] = service->icon();
                } else {
                    entry[QStringLiteral("Name")] = appId;
                    QString iconCandidate = appId.section(QStringLiteral("/"), -1);
                    if (QIcon::hasThemeIcon(iconCandidate)) {
                        entry[QStringLiteral("Icon")] = iconCandidate;
                    } else {
                        entry[QStringLiteral("Icon")] = QString();
                    }
                }
                entry[QStringLiteral("Reason")] = reason;
                snapshot[appId] = entry;
            }
            m_inhibitionMap = std::move(snapshot);
            rebuildInhibitions();
        }
    });
}

void KScreenApplet::onInhibitionsChanged(const QList<QPair<QString, QString>> &added, const QStringList &removed)
{
    ++m_policyRevision;
    for (const auto &info : added) {
        const QString &appId = info.first;
        const QString &reason = info.second;
        if (appId == QStringLiteral("plasmashell") || appId == QStringLiteral("plasmoidviewer")) {
            continue;
        }
        QVariantMap entry;
        KService::Ptr service = KService::serviceByStorageId(appId + QStringLiteral(".desktop"));
        if (service) {
            entry[QStringLiteral("Name")] = service->name();
            entry[QStringLiteral("Icon")] = service->icon();
        } else {
            entry[QStringLiteral("Name")] = appId;
            QString iconCandidate = appId.section(QStringLiteral("/"), -1);
            if (QIcon::hasThemeIcon(iconCandidate)) {
                entry[QStringLiteral("Icon")] = iconCandidate;
            } else {
                entry[QStringLiteral("Icon")] = QString();
            }
        }
        entry[QStringLiteral("Reason")] = reason;
        m_inhibitionMap[appId] = entry;
    }

    for (const auto &appId : removed) {
        m_inhibitionMap.remove(appId);
    }

    rebuildInhibitions();
}

void KScreenApplet::onPowerServiceOwnerChanged(const QString &, const QString &, const QString &newOwner)
{
    ++m_policyGeneration;
    ++m_policyQuerySerial;
    ++m_policyRevision;
    m_powerOwner = newOwner;
    if (!m_inhibitionMap.isEmpty()) {
        m_inhibitionMap.clear();
        rebuildInhibitions();
    }
    if (!newOwner.isEmpty()) {
        queryInhibitions();
    }
}

void KScreenApplet::onScreenSaverOwnerChanged(const QString &, const QString &, const QString &newOwner)
{
    ++m_screenSaverGeneration;
    ++m_presentationRequestSerial;
    m_screenSaverOwner = newOwner;

    const bool wasEnabled = m_cookie.has_value();
    const bool wasPending = m_presentationModePending;
    m_cookie.reset();
    m_cookieOwner.clear();
    setPresentationModePending(false);
    if (wasEnabled) {
        Q_EMIT presentationModeEnabledChanged();
    }
    if (wasPending) {
        Q_EMIT presentationModeOperationFailed(QStringLiteral("serviceRestart"),
                                               QStringLiteral("org.freedesktop.DBus.Error.NameHasNoOwner"),
                                               QStringLiteral("ScreenSaver service owner changed"));
    }
}

void KScreenApplet::rebuildInhibitions()
{
    QVariantList list;
    for (auto it = m_inhibitionMap.constBegin(); it != m_inhibitionMap.constEnd(); ++it) {
        list.append(it.value());
    }
    m_inhibitionList = std::move(list);
    Q_EMIT inhibitionsChanged();
}

void KScreenApplet::setPresentationModePending(bool pending)
{
    if (m_presentationModePending == pending) {
        return;
    }
    m_presentationModePending = pending;
    Q_EMIT presentationModePendingChanged();
}

QVariant KScreenApplet::availableActions()
{
    auto actions = KScreen::OsdAction::availableActions();
    QList<KScreen::OsdAction> ret;
    ret.reserve(actions.size() - 1);
    for (const auto &action : actions) {
        if (action.action != KScreen::OsdAction::NoAction) {
            ret.append(action);
        }
    }
    // Need to wrap it in a QVariant, otherwise QML doesn't like the return type
    return QVariant::fromValue(ret);
}

#include "kscreenapplet.moc"

#include "moc_kscreenapplet.cpp"
