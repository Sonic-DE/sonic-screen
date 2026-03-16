/*
    SPDX-FileCopyrightText: 2025 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <unordered_map>

class QQuickWindow;
class QEvent;
class ColorManagementGlobal;
class ColorManagementSurface;

class HdrHelper : public QObject
{
    Q_OBJECT
    QML_SINGLETON
    QML_ELEMENT
public:
    explicit HdrHelper();
    ~HdrHelper();

    enum class Colorspace {
        BT709Linear,
        BT2020PQ,
    };
    Q_ENUM(Colorspace);
    enum class RenderIntent {
        Perceptual,
        RelativeColorimetricBPC,
    };
    Q_ENUM(RenderIntent);

    Q_INVOKABLE void
    setHdrParameters(QQuickWindow *window, Colorspace colorspace, uint32_t referenceLuminance, uint32_t maximumLuminance, RenderIntent renderIntent);

private:
    void setImageDescription(QQuickWindow *window);
    bool eventFilter(QObject *watched, QEvent *event) override;

    const std::unique_ptr<ColorManagementGlobal> m_global;
    struct WindowParams {
        std::unique_ptr<ColorManagementSurface> surface;
        Colorspace colorspace;
        RenderIntent renderIntent;
        uint32_t referenceLuminance;
        uint32_t maximumLuminance;
    };
    std::unordered_map<QQuickWindow *, WindowParams> m_surfaces;
};

class ColorManagementGlobal
{
public:
    explicit ColorManagementGlobal();
};

class ColorManagementSurface : public QObject
{
    Q_OBJECT
public:
    explicit ColorManagementSurface(void *obj);
    ~ColorManagementSurface() override;
};

class PendingImageDescription
{
public:
    explicit PendingImageDescription(QQuickWindow *window,
                                     ColorManagementSurface *surface,
                                     void *descr,
                                     HdrHelper::RenderIntent renderIntent);
    ~PendingImageDescription();

    QPointer<QQuickWindow> m_window;
    QPointer<ColorManagementSurface> m_surface;
    HdrHelper::RenderIntent m_renderIntent;
};
