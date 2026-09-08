/*
    Work sponsored by the LiMux project of the city of Munich:
    SPDX-FileCopyrightText: 2018 Kai Uwe Broulik <kde@broulik.de>

    SPDX-License-Identifier: GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL
*/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents3
import org.kde.plasma.extras as PlasmaExtras

ColumnLayout {
    id: root

    required property var applet

    property bool requestedChecked: applet.presentationModeEnabled

    spacing: Kirigami.Units.smallSpacing

    PlasmaComponents3.Switch {
        id: presentationModeSwitch
        Layout.fillWidth: true
        // Remove spacing between checkbox and the explanatory label below
        Layout.bottomMargin: -root.spacing
        text: i18n("Enable Presentation Mode")

        checked: root.applet.presentationModePending ? root.requestedChecked : root.applet.presentationModeEnabled
        enabled: !root.applet.presentationModePending

        onToggled: {
            root.requestedChecked = checked
            if (checked) {
                root.applet.enablePresentationMode(i18n("User enabled presentation mode"))
            } else {
                root.applet.disablePresentationMode()
            }
        }
    }

    PlasmaExtras.DescriptiveLabel {
        Layout.fillWidth: true
        Layout.leftMargin: presentationModeSwitch.indicator.width + presentationModeSwitch.spacing
        font: Kirigami.Theme.smallFont
        text: i18n("This will prevent your screen and computer from turning off automatically.")
        wrapMode: Text.WordWrap
    }

    InhibitionHint {
        Layout.fillWidth: true
        Layout.leftMargin: presentationModeSwitch.indicator.width + presentationModeSwitch.spacing

        iconSource: root.applet.inhibitions.length > 0 ? root.applet.inhibitions[0].Icon || "" : ""
        text: {
            const inhibitions = root.applet.inhibitions
            const inhibition = inhibitions[0]
            if (inhibitions.length > 1) {
                return i18ncp("Some Application and n others enforce presentation mode",
                              "%2 and %1 other application are enforcing presentation mode.",
                              "%2 and %1 other applications are enforcing presentation mode.",
                              inhibitions.length - 1, inhibition.Name) // plural only works on %1
            } else if (inhibitions.length === 1) {
                if (!inhibition.Reason) {
                    return i18nc("Some Application enforce presentation mode",
                                 "%1 is enforcing presentation mode.", inhibition.Name)
                } else {
                    return i18nc("Some Application enforce presentation mode: Reason provided by the app",
                                 "%1 is enforcing presentation mode: %2", inhibition.Name, inhibition.Reason)
                }
            } else {
                return ""
            }
        }
    }
}
