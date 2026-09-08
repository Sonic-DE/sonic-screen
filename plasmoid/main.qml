/*
    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2018 Kai Uwe Broulik <kde@broulik.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.config as KConfig
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid

PlasmoidItem {
    id: root

    // Only show if the user enabled presentation mode. These members are
    // supplied by the C++ Plasma::Applet subclass and are absent from the
    // generic Plasma::Applet qmltypes description.
    // qmllint disable missing-property
    Plasmoid.status: Plasmoid.presentationModeEnabled ? PlasmaCore.Types.ActiveStatus : PlasmaCore.Types.PassiveStatus
    Plasmoid.icon: "preferences-desktop-display-randr-symbolic"
    toolTipSubText: Plasmoid.presentationModeEnabled ? i18n("Presentation mode is enabled") : ""
    // qmllint enable missing-property

    readonly property string kcmName: "kcm_kscreen"
    readonly property bool kcmAllowed: KConfig.KAuthorized.authorizeControlModule("kcm_kscreen")

    PlasmaCore.Action {
        id: configureAction
        text: i18n("Configure Display Settings…")
        icon.name: "preferences-desktop-display"
        visible: root.kcmAllowed
        onTriggered: KCMUtils.KCMLauncher.openSystemSettings(root.kcmName)
    }

    Component.onCompleted: {
        Plasmoid.setInternalAction("configure", configureAction);
    }

    fullRepresentation: ColumnLayout {
        spacing: 0
        Layout.preferredWidth: Kirigami.Units.gridUnit * 15

        ScreenLayoutSelection {
            Layout.leftMargin: Kirigami.Units.smallSpacing
            Layout.fillWidth: true
            screenLayouts: Plasmoid.availableActions // qmllint disable missing-property
        }

        PresentationModeItem {
            applet: Plasmoid
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing * 2
            Layout.leftMargin: Kirigami.Units.smallSpacing
        }

        // compact the layout
        Item {
            Layout.fillHeight: true
        }
    }
}
