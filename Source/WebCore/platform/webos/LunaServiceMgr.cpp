#include "config.h"
#if ENABLE(PALM_SERVICE_BRIDGE)
#include <glib.h>
#include "LunaServiceMgr.h"

#include <unistd.h>
#include "PlatformString.h"
#include <lunaservice.h>
#include <stdio.h>
#include <stdlib.h>
#include <wtf/text/CString.h>

namespace WebCore {

/** 
* @brief Internal callback for service responses.
* 
* @param  sh 
* @param  reply 
* @param  ctx 
* 
* @retval
*/
static bool 
message_filter(LSHandle *sh, LSMessage* reply, void* ctx)
{
    const char* payload = LSMessageGetPayload(reply);

    LunaServiceManagerListener* listener = (LunaServiceManagerListener*)ctx;

    if (listener) {
        listener->serviceResponse(payload);
        return true;
    }

    return false;
}

LunaServiceManager* s_instance = 0;

/** 
* @brief Obtains the singleton LunaServiceManager.
* 
* @retval the LunaServiceManager
*/
LunaServiceManager* LunaServiceManager::instance()
{
    bool retVal;
    if (s_instance)
        return s_instance;

    s_instance = new LunaServiceManager();
    retVal = s_instance->init();
    if (!retVal)
        goto error;

    return s_instance;

error:
    fprintf(stderr, "*******************************************************************\n");
    fprintf(stderr, "*  Could got get an instance of LunaServiceManager.               *\n");
    fprintf(stderr, "*  Try running with luna-dbus start; luna-dbus run <executable>.  *\n");
    fprintf(stderr, "*******************************************************************\n");
    exit(-1);
}
    
/** 
* @brief Private constructor to enforce singleton.
*/
LunaServiceManager::LunaServiceManager() :
      publicBus(0)
    , privateBus(0)
    , palmServiceHandle(0)
    , publicBusHighPriority(0)
    , privateBusHighPriority(0)
    , palmServiceHandleHighPriority(0)
{
}

LunaServiceManager::~LunaServiceManager()
{
    // ED : Close the single connection to DBUS.
    if (palmServiceHandle) {
        bool retVal;
        LSError lserror;
        LSErrorInit(&lserror);

        retVal = LSUnregisterPalmService(palmServiceHandle, &lserror);
        if (!retVal) {
            g_warning("LSUnregisterPalmService ERROR %d: %s (%s @ %s:%d)",
                lserror.error_code, lserror.message,
                lserror.func, lserror.file, lserror.line);
            LSErrorFree(&lserror);
        }
    }
}

bool LunaServiceManager::init()
{
    bool init;
    LSError lserror;
    LSErrorInit(&lserror);

    String id("com.palm.luna-");
    id.append(String::number(getpid()));    
    String active = (id + "-active");
    String phone = (id + "-phone");
    init = LSRegisterPalmService(id.utf8().data(), &palmServiceHandle, &lserror);
    if (!init) 
        goto error;
    
    init = LSGmainAttachPalmService(palmServiceHandle,
            g_main_loop_new(g_main_context_default(), TRUE), &lserror); 
    if (!init) 
        goto error;

    privateBus = LSPalmServiceGetPrivateConnection(palmServiceHandle);
    publicBus = LSPalmServiceGetPublicConnection(palmServiceHandle);

    if (privateBus) {
        init = LSGmainSetPriority(privateBus, G_PRIORITY_DEFAULT, &lserror);
        if (!init)
            goto error;
    }

    if (publicBus) {
        init = LSGmainSetPriority(publicBus, G_PRIORITY_DEFAULT, &lserror);
        if (!init)
            goto error;
    }

    init = LSRegisterPalmService(phone.utf8().data(), &palmServiceHandleHighPriority, &lserror);
    if (!init) 
        goto error;

    init = LSGmainAttachPalmService(palmServiceHandleHighPriority,
            g_main_loop_new(g_main_context_default(), TRUE), &lserror); 
    if (!init) 
        goto error;

    privateBusHighPriority = LSPalmServiceGetPrivateConnection(palmServiceHandleHighPriority);
    publicBusHighPriority = LSPalmServiceGetPublicConnection(palmServiceHandleHighPriority);

    if (privateBusHighPriority) {
        init = LSGmainSetPriority(privateBusHighPriority, G_PRIORITY_HIGH, &lserror);
        if (!init)
            goto error;
    }

    if (publicBusHighPriority) {
        init = LSGmainSetPriority(publicBusHighPriority, G_PRIORITY_HIGH, &lserror);
        if (!init)
            goto error;
    }


    init = LSRegisterPalmService(active.utf8().data(), &palmServiceHandleMediumPriority, &lserror);
    if (!init) 
        goto error;

    init = LSGmainAttachPalmService(palmServiceHandleMediumPriority,
            g_main_loop_new(g_main_context_default(), TRUE), &lserror);
    if (!init) 
        goto error;

    privateBusMediumPriority = LSPalmServiceGetPrivateConnection(palmServiceHandleMediumPriority);
    publicBusMediumPriority = LSPalmServiceGetPublicConnection(palmServiceHandleMediumPriority);

    if (privateBusMediumPriority) {
        init = LSGmainSetPriority(privateBusMediumPriority, G_PRIORITY_HIGH + 50, &lserror);
        if (!init)
            goto error;
    }

    if (publicBusMediumPriority) {
        init = LSGmainSetPriority(publicBusMediumPriority, G_PRIORITY_HIGH + 50, &lserror);
        if (!init)
            goto error;
    }

error:
    if (!init) {
        g_warning("Cannot initialize LunaServiceManager ERROR %d: %s (%s @ %s:%d)",
            lserror.error_code, lserror.message,
            lserror.func, lserror.file, lserror.line);
        LSErrorFree(&lserror);
    }

    return init;
}

/** 
* @brief This method will make the async call to DBUS.
* 
* @param  uri 
* @param  payload 
* @param  inListener 
* 
* @retval 0 if message could not be sent.
* @retval >0 serial number for the message.
*/
unsigned long LunaServiceManager::call(const char* uri, const char* payload, LunaServiceManagerListener* inListener,
                                       const char* callerId, bool usePrivateBus)
{
    bool retVal;
    LSError lserror;
    LSErrorInit(&lserror);
    LSMessageToken token = 0;
    LSHandle* serviceHandle = 0;
    
    if (callerId && (!(*callerId))) 
        callerId = 0;

    static int phoneAppIdLen = strlen("com.palm.app.phone");
    if (callerId && !(strncmp(callerId, "com.palm.app.phone", phoneAppIdLen))) {

        if (!usePrivateBus)
            serviceHandle = publicBusHighPriority;
        else
            serviceHandle = privateBusHighPriority;

    } else {
/*  else if (callerId && activeAppId && strncmp(callerId, activeAppId, strlen(activeAppId)) == 0) {


        if (!usePrivateBus)
            serviceHandle = publicBusMediumPriority;
        else
            serviceHandle = privateBusMediumPriority;
    }
*/
        if (!usePrivateBus) 
            serviceHandle = publicBus;
        else  
            serviceHandle = privateBus;
    }
    
    if (!inListener)
        retVal = LSCallFromApplication(serviceHandle, uri, payload, callerId, 0, 0, &token, &lserror);
    else {
        retVal = LSCallFromApplication(serviceHandle, uri, payload, callerId, message_filter, inListener, &token, &lserror);
        if (retVal) {
            inListener->listenerToken = token;
            inListener->sh = serviceHandle;
        }
    }

    if (!retVal) {
        g_warning("LSCallFromApplication ERROR %d: %s (%s @ %s:%d)",
            lserror.error_code, lserror.message,
            lserror.func, lserror.file, lserror.line);
        LSErrorFree(&lserror);
        token = 0;
        goto error;
    }

error:
    return token;
}
    
/** 
 * @brief Terminates a call causing any subscription for responses to end.
 *        This is also called by garbage collector's collect()
 *        when no more references to inListener exist.
 *
 * @param  inListener 
 */
void LunaServiceManager::cancel(LunaServiceManagerListener* inListener)
{
    bool retVal;
    LSError lserror;

    if (!inListener || !inListener->listenerToken)
        return;
    
    LSErrorInit(&lserror);
    
    if (!LSCallCancel(inListener->sh, inListener->listenerToken, &lserror)) {
        g_warning("LSCallCancel ERROR %d: %s (%s @ %s:%d)",
            lserror.error_code, lserror.message,
            lserror.func, lserror.file, lserror.line);
        LSErrorFree(&lserror);
    }

    // set the token to zero to indicate we have been canceled
    inListener->listenerToken = 0;
}
};
#endif
