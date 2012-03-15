#ifndef PalmServiceBridge_h
#define PalmServiceBridge_h

#ifdef ENABLE(PALM_SERVICE_BRIDGE)

#include "ActiveDOMObject.h"
#include "Event.h"
#include "EventListener.h"
#include "EventTarget.h"
#include "LunaServiceMgr.h"
#include <wtf/OwnPtr.h>

#if USE(V8)
#include "v8.h"
#else
#include <heap/Strong.h>
#include <heap/StrongInlines.h>
#endif

#include <glib.h>
#include <list>


namespace WebCore {

class Document;


class PalmServiceBridge : public RefCounted<PalmServiceBridge>,
                          public LunaServiceManagerListener,
                          public ActiveDOMObject {
    public:
        static PassRefPtr<PalmServiceBridge> create(ScriptExecutionContext* context, bool subscribe = false) { return adoptRef(new PalmServiceBridge(context, subscribe)); }
        bool init(Document*, bool subscribed = false);
        ~PalmServiceBridge();

        static int numHandlesForUrl(const char* appId);
        static void handlesForUrl(const char* appId, std::list<PalmServiceBridge*>& outHandles);

        virtual PalmServiceBridge* toPalmServiceBridge() { return this; }

        static void detachServices(Document*);
        static void cancelServices(Document*);

        String version();

        int token();

        int call(const String& uri, const String& payload, ExceptionCode&);
        void cancel(ExceptionCode&);

        // callback from LunaServiceManagerListener
        virtual void serviceResponse(const char* body);

        Document* document() const;

#if USE(V8)     
        static void WeakRefCallback(v8::Persistent<v8::Value>, void* parameter);
        void setOnservicecallback(v8::Handle<v8::Function>);
#else
        void setOnservicecallback(PassRefPtr<EventListener> eventListener) { m_callbackFunction = eventListener; }
        EventListener* onservicecallback() const { return m_callbackFunction.get(); }
#endif      

        virtual ScriptExecutionContext* scriptExecutionContext() const;

        // ActiveDOMObject:
        virtual void contextDestroyed();
        virtual bool canSuspend() const;
        virtual void stop();

    private:
#if USE(V8)
        v8::Persistent<v8::Function> m_callbackFunction;
#else
        RefPtr<EventListener> m_callbackFunction;
#endif
        bool m_canceled;
        bool m_subscribed;
        bool m_inServiceCallback;

        PalmServiceBridge(ScriptExecutionContext*, bool);
        PalmServiceBridge();
};

}

#endif
#endif
