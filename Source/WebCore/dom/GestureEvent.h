/*
 * Copyright (C) 2008, Apple Inc. All rights reserved.
 *
 * Permission is granted by Apple to use this file to the extent
 * necessary to relink with LGPL WebKit files.
 *
 * No license or rights are granted by Apple expressly or by
 * implication, estoppel, or otherwise, to Apple patents and
 * trademarks. For the sake of clarity, no license or rights are
 * granted by Apple expressly or by implication, estoppel, or otherwise,
 * under any Apple patents, copyrights and trademarks to underlying
 * implementations of any application programming interfaces (APIs)
 * or to any functionality that is invoked by calling any API.
 */

#ifndef GestureEvent_h
#define GestureEvent_h

#if ENABLE(IOS_GESTURE_EVENTS)

#include "EventTarget.h"
#include "MouseRelatedEvent.h"
#include <wtf/Platform.h>
#include <wtf/RefPtr.h>

namespace WebCore {

class GestureEvent : public MouseRelatedEvent {
public:
    static PassRefPtr<GestureEvent> create()
    {
        return adoptRef(new GestureEvent);
    }
    static PassRefPtr<GestureEvent> create(
                 const AtomicString& type, bool canBubble, bool cancelable, AbstractView* view, int detail,
                 int screenX, int screenY, int pageX, int pageY,
                 bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
                 PassRefPtr<EventTarget> target, float scale, float rotation, bool isSimulated = false)
    {
        return adoptRef(new GestureEvent(
                 type, canBubble, cancelable, view, detail,
                 screenX, screenY, pageX, pageY,
                 ctrlKey, altKey, shiftKey, metaKey,
                 target, scale, rotation, isSimulated));
    }
    virtual ~GestureEvent() { }

    void initGestureEvent(const AtomicString& type, bool canBubble, bool cancelable, AbstractView*, int detail,
        int screenX, int screenY, int clientX, int clientY,
        bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
        PassRefPtr<EventTarget>, float scale, float rotation);

    virtual const AtomicString& interfaceName() const;

    float scale() const { return m_scale; }
    float rotation() const { return m_rotation; }

private:
    GestureEvent() { }
    GestureEvent(const AtomicString& type, bool canBubble, bool cancelable, AbstractView*, int detail,
                 int screenX, int screenY, int pageX, int pageY,
                 bool ctrlKey, bool altKey, bool shiftKey, bool metaKey,
                 PassRefPtr<EventTarget>, float scale, float rotation, bool isSimulated = false);

    float m_scale;
    float m_rotation;
};

} // namespace WebCore

#endif


#endif // GestureEvent_h
