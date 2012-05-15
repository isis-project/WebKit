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

#ifndef PluginViewWebOs_h
#define PluginViewWebOs_h

#include <PluginView.h>
#include <qgraphicswebview.h>
#include <qobject.h>
#include <stdint.h>

QT_BEGIN_NAMESPACE
class QRect;
QT_END_NAMESPACE
class QGraphicsWebView;

namespace WebCore {

class PluginViewWebOs : public QObject, public PluginView {
    Q_OBJECT

public:

    static PassRefPtr<PluginViewWebOs> create(Frame* parentFrame, const IntSize&, HTMLPlugInElement*, const KURL&, const Vector<String>& paramNames, const Vector<String>& paramValues, const String& mimeType, bool loadManually);
    virtual ~PluginViewWebOs();

    virtual bool platformGetValue(NPNVariable, void* value, NPError* result);

signals:

    void addInterractiveWidgetRect(uintptr_t id, const QRect& frameRect, InteractiveRectType);
    void removeInterractiveWidgetRect(uintptr_t id, InteractiveRectType);
    void deadlockDetectionInterval(int);

protected:

    virtual void postPlatformStart(void);
    virtual void prePlatformDestroy(void);

private:

    PluginViewWebOs(Frame* parentFrame, const IntSize&, PluginPackage*, HTMLPlugInElement*, const KURL&, const Vector<String>& paramNames, const Vector<String>& paramValues, const String& mimeType, bool loadManually);
    void connectSignals();

    bool m_signalsConnected;
    bool m_isInteractive;
};

}

#endif

