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

#include "qwebevent.h"

QWebEvent::CustomEventTypes* QWebEvent::s_customTypes;

QWebEvent::QWebEvent(QWebEvent::Type type) : QEvent(QEvent::Type(type))
{
}

QWebIosGestureEvent::QWebIosGestureEvent(QWebEvent::Type type, const QPoint& pt, float rotation, float scale,
                                        bool shiftKey, bool ctrlKey, bool altKey, bool metaKey) :
    QWebEvent(type)
    , m_pt(pt)
    , m_rotation(rotation)
    , m_scale(scale)
    , m_shiftKey(shiftKey)
    , m_ctrlKey(ctrlKey)
    , m_altKey(altKey)
    , m_metaKey(metaKey)
{
}

/**
 * Register for our custom QEvent types. This instance (a singleton) is instantiated
 * lazily so that Qt is initialized prior to it's construction.
 */
QWebEvent::CustomEventTypes::CustomEventTypes() :
    iosGestureStartEventType(QEvent::registerEventType())
    , iosGestureChangeEventType(QEvent::registerEventType())
    , iosGestureEndEventType(QEvent::registerEventType())
{
}

QEvent::Type QWebEvent::getIosGestureStartEventType()
{
    if (!s_customTypes)
        s_customTypes = new CustomEventTypes();

    return static_cast<QEvent::Type>(s_customTypes->iosGestureStartEventType);
}

QEvent::Type QWebEvent::getIosGestureChangeEventType()
{
    if (!s_customTypes)
        s_customTypes = new CustomEventTypes();

    return static_cast<QEvent::Type>(s_customTypes->iosGestureChangeEventType);
}

QEvent::Type QWebEvent::getIosGestureEndEventType()
{
    if (!s_customTypes)
        s_customTypes = new CustomEventTypes();

    return static_cast<QEvent::Type>(s_customTypes->iosGestureEndEventType);
}
