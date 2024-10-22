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

#include "GestureEvent.h"

#include "NotImplemented.h"

#if ENABLE(IOS_GESTURE_EVENTS)

namespace WebCore {

GestureEvent::GestureEvent(const AtomicString& type, bool canBubble, bool cancelable, AbstractView* view, int detail,
                     int screenX, int screenY, int pageX, int pageY,
                     bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
                     PassRefPtr<EventTarget> target, float scale, float rotation, bool isSimulated)
: MouseRelatedEvent(type, canBubble, cancelable, view, detail,
            IntPoint(screenX, screenY), IntPoint(pageX, pageY), ctrlKey, altKey, shiftKey, metaKey, isSimulated)
{
    m_scale = scale;
    m_rotation = rotation;
}

void GestureEvent::initGestureEvent(const AtomicString& type, bool canBubble, bool cancelable, AbstractView* view, int detail,
            int screenX, int screenY, int clientX, int clientY,
            bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
            PassRefPtr<EventTarget> target, float scale, float rotation)
{
    notImplemented();
}

const AtomicString& GestureEvent::interfaceName() const
{
    return eventNames().interfaceForGestureEvent;
}

}

#endif
