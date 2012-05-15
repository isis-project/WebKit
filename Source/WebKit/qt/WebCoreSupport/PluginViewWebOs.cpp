/*
 * Copyright (C) 2011 Hewlett-Packard Development Company, L.P.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Hewlett-Packard Development Company, L.P.
 * nor the names of its contributors may be used to endorse or promote
 * products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#include "PluginViewWebOs.h"

#include "Frame.h"
#include "FrameView.h"
#include "HostWindow.h"
#include "PluginDatabase.h"
#include "QWebPageClient.h"

namespace WebCore {

PluginViewWebOs::PluginViewWebOs(Frame* parentFrame, const IntSize& size,
        PluginPackage* plugin, HTMLPlugInElement* element, const KURL& url,
        const Vector<String>& paramNames, const Vector<String>& paramValues,
        const String& mimeType, bool loadManually) :
    PluginView(parentFrame, size, plugin, element, url, paramNames, paramValues, mimeType, loadManually)
    , m_signalsConnected(false)
    , m_isInteractive(false)
{
}

PluginViewWebOs::~PluginViewWebOs()
{
}


bool PluginViewWebOs::platformGetValue(NPNVariable variable, void* value, NPError* result)
{
    switch (variable) {

    case npPalmApplicationIdentifier: {
        QWebPageClient* client = parentFrame()->view()->hostWindow()->platformPageClient();
        if (!client) {
            *result = NPERR_GENERIC_ERROR;
            return false;
        }
        // Original implementation should have passed a destination address/length to which
        // this call will write the data. That way the ownership of the data would have been
        // well known. The current contract is that this string must be used immedietly after
        // this call returns and since WebKit isn't thread safe we're OK with this implementation.
        const QString& id = client->appIdentifier();
        static char identifier[256];
        const size_t idSize = sizeof(identifier);
        strncpy(identifier, id.toAscii(), idSize);
        identifier[idSize-1] = '\0';
        *reinterpret_cast<void**>(value) = const_cast<char*>(identifier);
        *result = NPERR_NO_ERROR;
        return true;
    }

    case npPalmInputFieldFocused: {
        QWebPageClient* client = parentFrame()->view()->hostWindow()->platformPageClient();
        if (!client) {
            *result = NPERR_GENERIC_ERROR;
            return false;
        }

        QGraphicsWebView* view = qobject_cast<QGraphicsWebView*>(client->pluginParent());
        bool active = value;
        client->setInputMethodEnabled(active);
        client->setInputMethodHints(Qt::ImhNone);
        emit view->page()->microFocusChanged();
        return true;
    }

    default:
        return PluginView::platformGetValue(variable, value, result);
    }
}

void PluginViewWebOs::prePlatformDestroy()
{
    connectSignals(); // Lazily connect (if necessary) our signals before emitting
    emit removeInterractiveWidgetRect(reinterpret_cast<uintptr_t>(this), ::InteractiveRectPlugin);
}

void PluginViewWebOs::postPlatformStart()
{
    bool isInteractive = false;
    NPError err = plugin()->pluginFuncs()->getvalue(instance(),
                                            npPalmIsInteractive,
                                            &isInteractive);
    m_isInteractive = (err == NPERR_NO_ERROR && isInteractive) ? true: false;
    if (m_isInteractive) {
        QRect fr(frameRect());

        connectSignals(); // Lazily connect (if necessary) our signals before emitting
        emit addInterractiveWidgetRect(reinterpret_cast<uintptr_t>(this),
                                            fr, ::InteractiveRectPlugin);
    }

    int pluginDeadlockTimeout(-1);
    if (plugin()->pluginFuncs()->getvalue(instance(), npPalmScriptStuckTimeout, &pluginDeadlockTimeout) == NPERR_NO_ERROR) {

        connectSignals(); // Lazily connect (if necessary) our signals before emitting
        // Give the plugin an additional 5 seconds beyond the returned value to recover from a stuck script
        // before deadlock detection kills the browserserver.
        const int deadlockPadding = 5;
        emit deadlockDetectionInterval(pluginDeadlockTimeout + deadlockPadding);
    }
}

/**
 * Connect our signals to the Qt View that owns this view.
 */
void PluginViewWebOs::connectSignals()
{
    if (m_signalsConnected)
        return;

    QWebPageClient* client = parentFrame()->view()->hostWindow()->platformPageClient();
    QGraphicsWebView* view = qobject_cast<QGraphicsWebView*>(client->pluginParent());
    if (view && view->page()) {

        connect(this, SIGNAL(addInterractiveWidgetRect(uintptr_t, const QRect&, InteractiveRectType)),
                view->page(), SIGNAL(addInterractiveWidgetRect(uintptr_t, const QRect&, InteractiveRectType)));

        connect(this, SIGNAL(removeInterractiveWidgetRect(uintptr_t, InteractiveRectType)),
                view->page(), SIGNAL(removeInterractiveWidgetRect(uintptr_t, InteractiveRectType)));

        connect(this, SIGNAL(deadlockDetectionInterval(int)),
                view->page(), SIGNAL(deadlockDetectionInterval(int)));

        m_signalsConnected = true;
    }
}

/*
 * Identical to the version in PluginView.cpp - just instantiate a PluginViewWebOs class instead.
 */
PassRefPtr<PluginViewWebOs> PluginViewWebOs::create(Frame* parentFrame, const IntSize& size, HTMLPlugInElement* element, const KURL& url, const Vector<String>& paramNames, const Vector<String>& paramValues, const String& mimeType, bool loadManually)
{
    // if we fail to find a plugin for this MIME type, findPlugin will search for
    // a plugin by the file extension and update the MIME type, so pass a mutable String
    String mimeTypeCopy = mimeType;
    PluginPackage* plugin = PluginDatabase::installedPlugins()->findPlugin(url, mimeTypeCopy);

    // No plugin was found, try refreshing the database and searching again
    if (!plugin && PluginDatabase::installedPlugins()->refresh()) {
        mimeTypeCopy = mimeType;
        plugin = PluginDatabase::installedPlugins()->findPlugin(url, mimeTypeCopy);
    }

    return adoptRef(new PluginViewWebOs(parentFrame, size, plugin, element, url, paramNames, paramValues, mimeTypeCopy, loadManually));
}

PassRefPtr<PluginView> PluginView::create(Frame* parentFrame, const IntSize& size, HTMLPlugInElement* element, const KURL& url, const Vector<String>& paramNames, const Vector<String>& paramValues, const String& mimeType, bool loadManually)
{
    return PluginViewWebOs::create(parentFrame, size, element, url, paramNames, paramValues, mimeType, loadManually);
}

}

#include "moc_PluginViewWebOs.cpp"
