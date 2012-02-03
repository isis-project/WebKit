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

#ifndef qwebevent_h
#define qwebevent_h

#include "qwebkitglobal.h"
#include <QEvent>
#include <QPoint>

/**
 * A custom QEvent supported by this WebKit port.
 */
class QWEBKIT_EXPORT QWebEvent : public QEvent {

    public:

    QWebEvent(QEvent::Type);

    static QEvent::Type getIosGestureStartEventType();
    static QEvent::Type getIosGestureChangeEventType();
    static QEvent::Type getIosGestureEndEventType();

    private:

    struct CustomEventTypes {
        CustomEventTypes();

        int iosGestureStartEventType;
        int iosGestureChangeEventType;
        int iosGestureEndEventType;
    };

    static CustomEventTypes* s_customTypes;
};

/**
 * Encapsulates an iOS gesture event as defined by:
 *
 * http://developer.apple.com/library/IOS/#documentation/AppleApplications/Reference/SafariWebContent/HandlingEvents/HandlingEvents.html
 */
class QWEBKIT_EXPORT QWebIosGestureEvent : public QWebEvent {

    public:

    QWebIosGestureEvent(QWebEvent::Type, const QPoint& pt, float rotation, float scale,
                                        bool shiftKey, bool ctrlKey, bool altKey, bool metaKey);

    const QPoint& pt() const { return m_pt; }
    float rotation() const { return m_rotation; }
    float scale() const { return m_scale; }
    bool shiftKey() const { return m_shiftKey; }
    bool ctrlKey() const { return m_ctrlKey; }
    bool altKey() const { return m_altKey; }
    bool metaKey() const { return m_metaKey; }

    private:

    QPoint m_pt;
    float m_rotation;
    float m_scale;
    bool m_shiftKey;
    bool m_ctrlKey;
    bool m_altKey;
    bool m_metaKey;
};

#endif
