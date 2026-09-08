/*
    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2018 Kai Uwe Broulik <kde@broulik.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

#pragma once

#include <Plasma/Applet>

#include <KScreen/Types>

#include "common/osdaction.h"

#include <QMap>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>
#include <optional>

struct OsdActionForeign : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(OsdAction)
    QML_UNCREATABLE("Only for enums")
    QML_EXTENDED_NAMESPACE(KScreen::OsdAction)
};

class QDBusServiceWatcher;
class OrgKdeSolidPowerManagementPolicyAgentInterface;

class KScreenApplet : public Plasma::Applet {
    Q_OBJECT

    /**
     * The number of currently connected (not necessarily enabled) outputs
     */
    Q_PROPERTY(int connectedOutputCount READ connectedOutputCount NOTIFY connectedOutputCountChanged FINAL)
    Q_PROPERTY(QVariant availableActions READ availableActions CONSTANT STORED false FINAL)

    Q_PROPERTY(QVariantList inhibitions READ inhibitions NOTIFY inhibitionsChanged FINAL)
    Q_PROPERTY(bool presentationModeEnabled READ presentationModeEnabled NOTIFY presentationModeEnabledChanged FINAL)
    Q_PROPERTY(bool presentationModePending READ presentationModePending NOTIFY presentationModePendingChanged FINAL)

public:
    explicit KScreenApplet(QObject *parent, const KPluginMetaData &data, const QVariantList &args);
    ~KScreenApplet() override;

    void init() override;

    int connectedOutputCount() const;

    Q_INVOKABLE void applyLayoutPreset(KScreen::OsdAction::Action action);

    Q_INVOKABLE void enablePresentationMode(const QString &reason);
    Q_INVOKABLE void disablePresentationMode();

    QVariantList inhibitions() const;
    bool presentationModeEnabled() const;
    bool presentationModePending() const;

    static QVariant availableActions();

Q_SIGNALS:
    void connectedOutputCountChanged();
    void inhibitionsChanged();
    void presentationModeEnabledChanged();
    void presentationModePendingChanged();
    void presentationModeOperationFailed(const QString &operation, const QString &errorName, const QString &errorMessage);

private:
    void checkOutputs();

    void queryInhibitions();
    void onPowerServiceOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void onScreenSaverOwnerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);

private Q_SLOTS:
    void onInhibitionsChanged(const QList<QPair<QString, QString>> &added, const QStringList &removed);

private:
    void rebuildInhibitions();

    void setPresentationModePending(bool pending);
    static void releaseCookie(const QString &owner, uint cookie);

    KScreen::ConfigPtr m_screenConfiguration;
    int m_connectedOutputCount = 0;

    QMap<QString, QVariantMap> m_inhibitionMap;
    QVariantList m_inhibitionList;

    std::optional<uint> m_cookie;
    QString m_cookieOwner;
    bool m_presentationModePending = false;
    QString m_powerOwner;
    QString m_screenSaverOwner;
    quint64 m_policyGeneration = 0;
    quint64 m_policyRevision = 0;
    quint64 m_policyQuerySerial = 0;
    quint64 m_screenSaverGeneration = 0;
    quint64 m_presentationRequestSerial = 0;
    QDBusServiceWatcher *m_powerWatcher = nullptr;
    QDBusServiceWatcher *m_screenSaverWatcher = nullptr;
};
