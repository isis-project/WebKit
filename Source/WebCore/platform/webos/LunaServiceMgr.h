
#ifndef LunaServiceMgr_h
#define LunaServiceMgr_h

#if ENABLE(PALM_SERVICE_BRIDGE)

#include <luna-service2/lunaservice.h>

namespace WebCore {

struct LunaServiceManagerListener {
        LunaServiceManagerListener() : listenerToken(LSMESSAGE_TOKEN_INVALID), sh(0) { }
        virtual ~LunaServiceManagerListener() { }
        virtual void serviceResponse(const char* body) = 0;
        LSMessageToken listenerToken;
        LSHandle* sh;
};


//
//  LunaServiceManager
//
// This class is a singleton which handles all the client requests
// for a WebKit instance.

class LunaServiceManager {
    public:
        ~LunaServiceManager();
        
        static LunaServiceManager* instance();
        unsigned long call(const char* uri, const char* payload, LunaServiceManagerListener*, const char* callerId, bool usePrivateBus = false);
        void cancel(LunaServiceManagerListener*);

    private:
        bool init();
        LunaServiceManager();

        LSHandle* publicBus;
        LSHandle* privateBus;
        LSPalmService* palmServiceHandle;

        // The Medium Priority bus is used for the active app
        LSHandle* publicBusMediumPriority;
        LSHandle* privateBusMediumPriority;
        LSPalmService* palmServiceHandleMediumPriority;

        // The High Priority bus is used only for the Phone app
        LSHandle* publicBusHighPriority;
        LSHandle* privateBusHighPriority;
        LSPalmService* palmServiceHandleHighPriority;
};

}

#endif
#endif
