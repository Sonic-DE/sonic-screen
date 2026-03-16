/*
    SPDX-FileCopyrightText: 2025 Xaver Hugl <xaver.hugl@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "hdrhelper.h"

#include <QQuickWindow>
#include <qpa/qplatformwindow_p.h>

ColorManagementGlobal::ColorManagementGlobal()
{
}

ColorManagementSurface::ColorManagementSurface(void *obj)
{
    (void)obj;
}

ColorManagementSurface::~ColorManagementSurface()
{
}

PendingImageDescription::PendingImageDescription(QQuickWindow *window,
                                                 ColorManagementSurface *surface,
                                                 void *descr,
                                                 HdrHelper::RenderIntent renderIntent)
    : m_window(window)
    , m_surface(surface)
    , m_renderIntent(renderIntent)
{
}

PendingImageDescription::~PendingImageDescription()
{
}

HdrHelper::HdrHelper()
    : m_global(std::make_unique<ColorManagementGlobal>())
{
}

HdrHelper::~HdrHelper()
{
}

void HdrHelper::setHdrParameters(QQuickWindow *window, Colorspace colorspace, uint32_t referenceLuminance, uint32_t maximumLuminance, RenderIntent renderIntent)
{
    if (!m_surfaces.contains(window)) {
        connect(window, &QObject::destroyed, this, [this, window]() {
            m_surfaces.erase(window);
        });
    }
    auto &params = m_surfaces[window];
    params.colorspace = colorspace;
    params.renderIntent = renderIntent;
    params.referenceLuminance = referenceLuminance;
    params.maximumLuminance = maximumLuminance;

    setImageDescription(window);
}

void HdrHelper::setImageDescription(QQuickWindow *window)
{
    auto &params = m_surfaces[window];
    if (params.colorspace == Colorspace::BT2020PQ) {
    } else {
    }
    new PendingImageDescription(window, params.surface.get(), nullptr, params.renderIntent);
}

bool HdrHelper::eventFilter(QObject *watched, QEvent *event)
{
    auto window = qobject_cast<QQuickWindow *>(watched);
    if (!window) {
        return false;
    }
    if (event->type() == QEvent::PlatformSurface) {
        auto surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
            setImageDescription(window);
        }
    }
    return false;
}

#include "moc_hdrhelper.cpp"
