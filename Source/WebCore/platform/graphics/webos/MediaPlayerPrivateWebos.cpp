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
#include "MediaPlayerPrivateWebos.h"

#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/shm.h>

#if ENABLE(VIDEO)

// Do we want to pass scale factor to remote openGL?
// #define MEDIAPLAYER_PALM_SCALE_REMOTE_VIDEO
#ifdef MEDIAPLAYER_PALM_SCALE_REMOTE_VIDEO
// If uses remote openGL to scale, need this for triggering piranha fast copy path
#define MEDIAPLAYER_PALM_WORKAROUND_PIRANHA_SCALE_BUG
#endif

#include "CookieJar.h"
#include "Frame.h"
#include "FrameLoader.h"
#include "FrameLoaderClient.h"
#include "GraphicsContext.h"
#include "NotImplemented.h"
#include "Settings.h"
#include <media/Media.h>
#include <wtf/text/CString.h>

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
#include "TextureMapperGL.h"
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <QGLContext>
#endif

using namespace std;
using namespace pbnjson;

const char* s_playButtonImgPath = "/images/playbutton";

#define TRACE_DEBUG_THIS_FILE  0
#define USE_COLOR_PRINTF       1

#if TRACE_DEBUG_THIS_FILE
#define SHOW_OVERLAY_CAPTURE 1
#if USE_COLOR_PRINTF
#define COLORESCAPE             "\033["
#define RESETCOLOR              COLORESCAPE "0m"
#define BOLDCOLOR               COLORESCAPE "1m"
#define REDOVERBLACK    COLORESCAPE "1;31m"
#define BLUEOVERBLACK   COLORESCAPE "1;34m"
#define YELLOWOVERBLACK COLORESCAPE "1;33m"
#else
#define COLORESCAPE
#define RESETCOLOR
#define BOLDCOLOR
#define REDOVERBLACK
#define BLUEOVERBLACK
#define YELLOWOVERBLACK
#endif
#define PRINTF_RED(fmt, args...) printf(REDOVERBLACK fmt RESETCOLOR "\n", ## args)
#define PRINTF_BLUE(fmt, args...) printf(BLUEOVERBLACK fmt RESETCOLOR "\n", ## args)
#define PRINTF_YELLOW(fmt, args...) printf(YELLOWOVERBLACK fmt RESETCOLOR "\n", ## args)
#define PRINTF_BOLD(fmt, args...) printf(BOLDCOLOR fmt RESETCOLOR "\n", ## args)
#define CHECK(t) (t || (PRINTF_YELLOW("%s: '%s' failed CHECK.", __FUNCTION__, #t), false))
#define VERIFY(t) (t || (PRINTF_RED("%s: '%s' failed VERIFY.", __FUNCTION__, #t), false))

static const char * getTimeStamp()
{
    static bool sInited = false;
    static struct timespec sLogStartSeconds = {0};
    static struct tm sLogStartTime = {0};
    if (!sInited) {
        time_t now = ::time(0);
        ::clock_gettime(CLOCK_MONOTONIC, &sLogStartSeconds);
        ::localtime_r(&now, &sLogStartTime);
        sInited = true;
    }
    struct timespec now;
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    int ms = (now.tv_nsec - sLogStartSeconds.tv_nsec) / 1000000;
    int sec = sLogStartTime.tm_sec + int(now.tv_sec - sLogStartSeconds.tv_sec);
    if (ms < 0) {
        ms += 1000;
        --sec;
    }
    int min = sLogStartTime.tm_min + sec / 60;
    int hr = sLogStartTime.tm_hour + min / 60;
    min = min % 60;
    sec = sec % 60;
    static char sTimeStamp[128];
    size_t len = ::snprintf(sTimeStamp, sizeof(sTimeStamp), "%02d:%02d:%02d.%03d", hr, min, sec, ms);
    return sTimeStamp;
}

#else
#define SHOW_OVERLAY_CAPTURE 0
#define PRINTF_RED(fmt, args...) (void)0
#define PRINTF_BLUE(fmt, args...) (void)0
#define PRINTF_YELLOW(fmt, args...) (void)0
#define PRINTF_BOLD(fmt, args...) (void)0
#define CHECK(t) (void)0
#define VERIFY(t) (void)0
#endif

#if !defined(NDEBUG)
#define TRACE(fmt, args...) printf("MTRACE: (%s:%d) %s: " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args)
#else
#define TRACE(fmt, args...) (void)0
#endif

/** Define this to get more verbose logging. */
#undef VERBOSE_MESSAGES
#if defined(VERBOSE_MESSAGES)
#define VERBOSE_TRACE(fmt, args...) TRACE(fmt, ## args)
#else
#define VERBOSE_TRACE(fmt, args...) (void)0
#endif

#define INVOKE_PLAYER(method, ...) {\
    PRINTF_BLUE("%s Invoke player " #method "()" , __PRETTY_FUNCTION__); \
    m_client->method(__VA_ARGS__); \
}

#define INVOKE_CAPTURE(method, ...) {\
    PRINTF_BLUE("%s Invoke capture " #method "()" , __PRETTY_FUNCTION__); \
    m_capture->method(__VA_ARGS__); \
}

#if PLATFORM(QT)
#include <QAbstractEventDispatcher>
#include <QImage>
#include <QPainter>
#define CreateNativeSurface(width, height)  (new QImage(width, height, QImage::Format_ARGB32))
#define GetNativeSurfaceBits(p) (p->constBits())
#define WrapNativeSurface(data, width, height, bytesPerLine)  (new QImage((const uchar*)data, width, height, bytesPerLine, QImage::Format_ARGB32_Premultiplied))
#define ReleaseNativeSurface(p) delete(p)
#define DrawNativeSurface(gc, p, s_left, s_top, s_width, s_height, d_left, d_top, d_width, d_height) \
    gc->platformContext()->drawImage(QRectF(d_left, d_top, d_width, d_height), \
                                     *p, \
                                     QRectF(s_left, s_top, s_width, s_height))
#endif

namespace WebCore {

    // abstract class for both capture and playback
class MediaChangeListener {
public:
    MediaChangeListener(WebCore::MediaPlayerPrivate *p):m_mpp(p), m_makeReadyForDisplay(false) { };
    virtual ~MediaChangeListener()
    {
        PRINTF_BLUE("%s", __PRETTY_FUNCTION__);
    }

protected:
    MediaPlayerPrivate* m_mpp;
    bool m_makeReadyForDisplay;

    void makeReadyForDisplay()
    {
        if (m_makeReadyForDisplay && !m_mpp->m_readyForDisplay) {
            PRINTF_RED("******** Ready for display at %s! ********", getTimeStamp());
            m_mpp->m_readyForDisplay = true;
            if (m_mpp->m_readyState == MediaPlayer::HaveEnoughData)
                m_mpp->m_player->readyStateChanged();
            m_mpp->m_player->repaint();
        }
        m_makeReadyForDisplay = false;
    };

};

class MediaCaptureV3ChangeListener: public media::MediaCaptureV3ChangeListener, public MediaChangeListener {
public:
    MediaCaptureV3ChangeListener(WebCore::MediaPlayerPrivate *p): MediaChangeListener(p)
    {
    }
    virtual ~MediaCaptureV3ChangeListener() { }
    virtual void readyChanged()
    {
        if (m_mpp->m_capture->getReady())
            makeReadyForDisplay();
    };
};

class ClonkChangeListener: public media::ClonkChangeListener, public MediaChangeListener {
public:
    ClonkChangeListener(WebCore::MediaPlayerPrivate *p): MediaChangeListener(p)
    {
    }
    virtual ~ClonkChangeListener() { }
    virtual void pipChanged()
    {
        PRINTF_YELLOW("~~ in pipChanged!! ~~");
        boost::shared_ptr<media::PipSettings> pip = m_mpp->m_clonk->getPip();
        long x = pip->top_left->x;
        long y = pip->top_left->y;
        long bottom = pip->bottom_right->y;
        long right = pip->bottom_right->x;
        PRINTF_YELLOW("~~ new pip dimensions: (%ld, %ld) (%ld, %ld) ~~", x, y, bottom, right);
        m_mpp->m_pipRect = IntRect(x, y, right - x, bottom - y);
        m_mpp->m_player->repaint();
    }
    virtual void videoCaptureActiveChanged()
    {
        PRINTF_YELLOW("~~ in videoCaptureActiveChanged ~~");
        m_mpp->m_pipVideoActive = m_mpp->m_clonk->getVideoCaptureActive();
        if (m_mpp->m_pipVideoActive) {
            PRINTF_YELLOW("~~ videoCapture (PiP) is active ~~");
            makeReadyForDisplay();
        }
    };
    virtual void videoPlayerActiveChanged()
    {
        PRINTF_YELLOW("~~ in videoPlayerActiveChanged ~~");
        m_mpp->m_fullVideoActive = m_mpp->m_clonk->getVideoPlayerActive();
        if (m_mpp->m_fullVideoActive) {
            PRINTF_YELLOW("~~ videoPlayer (large picture) is active ~~");
            makeReadyForDisplay();
        }
    };
    void makeReadyForDisplay()
    {
        PRINTF_RED("******** Ready for display at %s! ********", getTimeStamp());
        m_mpp->m_readyForDisplay = true;
        if (m_mpp->m_readyState == MediaPlayer::HaveEnoughData)
            m_mpp->m_player->readyStateChanged();
        m_mpp->m_player->repaint();
    }
};

class MediaPlayerChangeListener: public media::MediaPlayerChangeListener, public MediaChangeListener {
public:
    MediaPlayerChangeListener(WebCore::MediaPlayerPrivate *p): MediaChangeListener(p) { }
    virtual ~MediaPlayerChangeListener() { }

    void trimVideoTitleChanged()
    {
    };
    void fitModeChanged()
    {
        m_mpp->m_fitMode = m_mpp->m_client->getFitMode();
    };
    void savedChanged()
    {
        PRINTF_YELLOW("SAVED!");
    };
    void networkStateChanged()
    {
        switch (m_mpp->m_client->getNetworkState()) {
        default:
        case media::NETWORK_EMPTY:
            m_mpp->m_networkState = MediaPlayer::Empty;
            break;
        case media::NETWORK_IDLE:
            m_mpp->m_networkState = MediaPlayer::Idle;
            break;
        case media::NETWORK_LOADING:
            m_mpp->m_networkState = MediaPlayer::Loading;
            break;
        case media::NETWORK_LOADED:
            m_mpp->m_networkState = MediaPlayer::Loaded;
            break;
        }
        m_mpp->m_player->networkStateChanged();
    };
    void readyStateChanged()
    {
        switch (m_mpp->m_client->getReadyState()) {
        default:
        case media::HAVE_NOTHING:
            m_mpp->m_readyState = MediaPlayer::HaveNothing;
            break;
        case media::HAVE_METADATA:
            m_mpp->m_readyState = MediaPlayer::HaveMetadata;
            break;
        case media::HAVE_CURRENT_DATA:
            m_mpp->m_readyState = MediaPlayer::HaveCurrentData;
            m_makeReadyForDisplay = true;
            break;
        case media::HAVE_FUTURE_DATA:
            m_mpp->m_readyState = MediaPlayer::HaveFutureData;
            break;
        case media::HAVE_ENOUGH_DATA:
            m_mpp->m_readyState = MediaPlayer::HaveEnoughData;
            break;
        }
        m_mpp->m_player->readyStateChanged();
        makeReadyForDisplay();
    };
    void extendedErrorChanged()
    {
        string valueStr = m_mpp->m_client->getExtendedError();
        if (m_mpp->m_element) {
            ExceptionCode ec;
            m_mpp->m_element->setAttribute("x-palm-media-extended-error", valueStr.c_str(), ec);
        }
    };
    void errorChanged()
    {
        media::Error e = m_mpp->m_client->getError();
        if (media::NO_ERROR == e) {
            if (m_mpp->m_element) {
                ExceptionCode ec;
                m_mpp->m_element->setAttribute("x-palm-media-extended-error", "", ec);
            }
        } else {
            if (e == media::INVALID_SOURCE_ERROR) {
                m_mpp->m_networkState = MediaPlayer::FormatError;
                m_mpp->m_player->networkStateChanged();
            } else if (e == media::NETWORK_ERROR) {
                m_mpp->m_networkState = MediaPlayer::NetworkError;
                m_mpp->m_player->networkStateChanged();
            } else if (e == media::FORMAT_ERROR) {
                m_mpp->m_networkState = MediaPlayer::FormatError;
                m_mpp->m_player->networkStateChanged();
            } else if (e == media::DECODE_ERROR) {
                m_mpp->m_networkState = MediaPlayer::DecodeError;
                m_mpp->m_player->networkStateChanged();
            }
        }
    };
    void totalBytesChanged()
    {
        m_mpp->m_totalBytes = m_mpp->m_client->getTotalBytes();
    };
    void bytesLoadedChanged()
    {
        m_mpp->m_bytesLoaded = m_mpp->m_client->getBytesLoaded();
    };
    void durationChanged()
    {
        m_mpp->m_duration = m_mpp->m_client->getDuration();
        if (m_mpp->m_duration < 0)
            m_mpp->m_duration = numeric_limits<float>::infinity();

        PRINTF_BLUE("MediaPlayerChangeListener::durationChanged %f", m_mpp->m_duration);
        m_mpp->m_player->durationChanged();
    };
    void startTimeChanged()
    {
        PRINTF_RED("MediaPlayerChangeListener::startTimeChanged Removed from HTML5 spec ");
    };
    void endTimeChanged()
    {
        PRINTF_RED("MediaPlayerChangeListener::endTimeChanged Removed from HTML5 spec ");
    };
    void pausedChanged()
    {
        m_mpp->m_paused = m_mpp->m_client->getPaused();
    };
    void pausedChangeReasonChanged()
    {
        string reason = m_mpp->m_client->getPausedChangeReason();
        bool pause = m_mpp->m_client->getPaused();

        if ("paused true by arbitrator" == reason) {
            if (m_mpp->m_paused)
                m_mpp->m_element->pause(); // FIXME converted from pause(true)
        } else if ("paused false by arbitrator" == reason) {
            if (!m_mpp->m_paused)
                m_mpp->m_element->play(); // FIXME: converted from play(false)
        }
        if (!m_mpp->m_paused && !m_mpp->m_overlayplayback) {
            // only release non overlay playback capture
            // overlay playback capture is released when media process sent us a message
            if (m_mpp->m_videoFrameCaptured) {
                ReleaseNativeSurface(m_mpp->m_videoFrameCaptured);
                m_mpp->m_videoFrameCaptured = 0;
            }
        }
    };
    void currentTimeChanged()
    {
        m_mpp->m_currentTime = m_mpp->m_client->getCurrentTime();
        m_makeReadyForDisplay = TRUE;
        makeReadyForDisplay();
    };
    void eosChanged()
    {
        if (m_mpp->m_client->getEos()) {
            // pausing first as a workaround to ensure file continues playing after seek
            if (m_mpp->m_element->loop())
                m_mpp->m_player->pause();

            /*
             * we need to set the current time
             * to end time before notifying that the time
             * changed for the HTMLMediaElement to generate
             * the 'ended' event.
             */
            m_mpp->m_currentTime = m_mpp->m_duration;
            m_mpp->m_player->timeChanged();
        }
    };
    void seekingChanged()
    {
        m_mpp->m_seeking = m_mpp->m_client->getSeeking();
        if (!m_mpp->m_seeking) {
            m_mpp->m_player->timeChanged();
            m_makeReadyForDisplay = TRUE;
            makeReadyForDisplay();
        }
    };
    void hasVideoChanged()
    {
        m_mpp->m_hasVideo = m_mpp->m_client->getHasVideo();
    };
    void hasAudioChanged()
    {
        m_mpp->m_hasAudio = m_mpp->m_client->getHasAudio();
    };
    void pausableChanged()
    {
        m_mpp->m_pausable = m_mpp->m_client->getPausable();
        ExceptionCode ec;
        m_mpp->m_element->setAttribute("x-palm-media-pausable", m_mpp->m_pausable?"true":"false", ec);
    };
    void videoFrameCaptureChanged()
    {
        string tokSharedMem = m_mpp->m_client->getVideoFrameCapture();
        PRINTF_BLUE("videoFrameCapture: %s", tokSharedMem.c_str());

        if (!tokSharedMem.size()) {
            // playing: don't show anything and let the overlay shine
            if (m_mpp->m_videoFrameCaptured) {
                ReleaseNativeSurface(m_mpp->m_videoFrameCaptured);
                m_mpp->m_videoFrameCaptured = 0;
            }
            m_mpp->m_player->repaint();
        } else if ("-" == tokSharedMem) {
            // Magic value to ignore.
            PRINTF_BLUE("Ignoring videoFrameCapture message");
        } else {
            // use the bitmap data passed in shared memory
            const char * cFailedCapture = "?"; // TODO: how do we share this with mediaserver

            bool success = false;
            VERIFY(tokSharedMem != cFailedCapture);
            if (tokSharedMem != cFailedCapture) {
                bool isSysV =  (!tokSharedMem.compare(0, 5 /*"SYSV:".length*/, "SYSV:"));
                bool isPosix = !isSysV; // tokSharedMem.compare(0, 6 /*"POSIX:".length*/, "POSIX:");

                if (isPosix) {
                    string shmName = tokSharedMem.substr(6);

                    int fd = shm_open(shmName.c_str(), O_RDONLY, S_IRUSR | S_IWUSR);
                    VERIFY(fd >= 0);
                    if (fd >= 0) {
                        struct stat statbuf;
                        bool fstatok = !fstat(fd, &statbuf);
                        VERIFY(fstatok);
                        if (fstatok) {
                            VERIFY(statbuf.st_size > 2 * (int) sizeof(int));
                            if (statbuf.st_size > 2 * (int) sizeof(int)) {
                                int * ptr = (int *) mmap(0, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
                                VERIFY(ptr != MAP_FAILED && ptr);
                                if (ptr != MAP_FAILED && ptr) {
                                    int width = *(ptr++);
                                    int height = *(ptr++);
                                    bool dimensionsLookGood = width > 0 && width < 10000 && height > 0 && height < 10000;
                                    bool dataFitsInSharedMemory = width * height * 3 + 2 * (int) sizeof(int) <= statbuf.st_size;
                                    VERIFY(dimensionsLookGood);
                                    VERIFY(dataFitsInSharedMemory);
                                    if (dimensionsLookGood && dataFitsInSharedMemory) {
                                        int * dst = m_mpp->getVideoFrameCaptureData(width, height);
                                        VERIFY(dst);
                                        if (dst) {
                                            const char * src = (const char *) ptr;
                                            const char * end = src + 3 * width * height;
                                            while (src < end) {
                                                int data = *src++;
                                                data = (*src++ << 8) | data;
                                                data = (*src++ << 16) | data;
                                                *(dst++) = data | 0xff000000; // transparency
                                            }

                                            PRINTF_BLUE("Got video frame capture data from POSIX memory!");
                                            success = true;
                                        }
                                    }
                                    munmap(ptr, statbuf.st_size);
                                }
                            }
                        }
                    }
                    if (fd >= 0)
                        close(fd);
                } else if (isSysV) {
                    int shmMemId = atoi(tokSharedMem.substr(5).c_str());
                    PRINTF_BLUE("SysV ID for shared memory is %i", shmMemId);

                    struct shmid_ds buf;
                    // get the size of the shared memory created
                    int result = shmctl(shmMemId, IPC_STAT, &buf);
                    VERIFY(result != -1);
                    if (result != -1) {
                        VERIFY(buf.shm_segsz > 2 * (int) sizeof(int));
                        if (buf.shm_segsz > 2 * (int) sizeof(int)) {
                            const void * shmMemAddr = shmat(shmMemId, 0, SHM_RND);
                            VERIFY(shmMemAddr != MAP_FAILED && shmMemAddr);
                            if (shmMemAddr != MAP_FAILED && shmMemAddr) {
                                const int * ptr = (const int *) shmMemAddr;
                                int width = *(ptr++);
                                int height = *(ptr++);
                                bool dimensionsLookGood = width > 0 && width < 10000 && height > 0 && height < 10000;
                                bool dataFitsInSharedMemory = width * height * 3 + 2 * sizeof(int) <= buf.shm_segsz;
                                VERIFY(dimensionsLookGood);
                                VERIFY(dataFitsInSharedMemory);
                                if (dimensionsLookGood && dataFitsInSharedMemory) {
                                    int * dst = m_mpp->getVideoFrameCaptureData(width, height);
                                    VERIFY(dst);
                                    if (dst) {
                                        const char * src = (const char *) ptr;
                                        const char * end = src + 3 * width * height;
                                        while (src < end) {
                                            int data = *src++;
                                            data = (*src++ << 8) | data;
                                            data = (*src++ << 16) | data;
                                            *(dst++) = data | 0xff000000; // transparency
                                        }

                                        PRINTF_BLUE("Got video frame capture data from Sys V shared memory!");
                                        success = true;
                                    }
                                }
                                shmdt(shmMemAddr);
                            }
                        }
                    }
                } else
                    PRINTF_RED("Shared Memory ID did not decode to a valid format (SYS V or POSIX)");
            }
            if (!success) {
                PRINTF_BLUE("Failed to get video frame capture data: using black...");
                int width = m_mpp->m_width > 0 ? m_mpp->m_width : m_mpp->m_element->clientWidth();
                int height = m_mpp->m_height > 0 ? m_mpp->m_height : m_mpp->m_element->clientHeight();
                int * data = m_mpp->getVideoFrameCaptureData(width, height);
                if (data) {
                    int * dataEnd = data + width * height;
                    while (data < dataEnd)
                        *(data++) = 0xff000000; // no transparency, all the rest 0 = black
                }
            }
            if (m_mpp->m_overlayplayback && m_mpp->m_paused && m_mpp->m_unloading) {
                PRINTF_YELLOW("Received card event and the pipeline is paused, suspend!");
                m_mpp->m_client->suspend();
                m_mpp->m_unloading = false;
            }
            m_mpp->m_player->repaint();
        }
    };

}; // MediaPlayerChangeListener

} // namespace WebCore


namespace WebCore {

MediaPlayerPrivate::MediaPlayerPrivate(MediaPlayer* player)
    : m_player(player)
    , m_currentRemoteBuffer(-1)
    , m_updatesPending(0)
    , m_remoteBufferScale(1)
    , m_lastGcScale(1)
    , m_remoteBufferResizePending(false)
    , m_element(0)
    , m_videoFrameCaptured(0)
    , m_networkState(MediaPlayer::Empty)
    , m_readyState(MediaPlayer::HaveNothing)
    , m_readyForDisplay(false)
    , m_serverHandle(0)
    , m_bytesLoaded(0)
    , m_totalBytes(0)
    , m_currentTime(0)
    , m_duration(std::numeric_limits<float>::quiet_NaN())
    , m_paused(true)
    , m_seeking(false)
    , m_hasAudio(false)
    , m_hasVideo(false)
    , m_unloading(false)
    , m_lastLoadingUri("")
    , m_loadPendingUri(false)
    , m_currentPlayer("")
    , m_overlayplayback(false)
    , m_width(0)
    , m_height(0)
    , m_fitMode(media::VIDEO_FIT)
    , m_pausable(true)
    , m_pipRect(0, 0, 0, 0)
    , m_pipVideoActive(false)
    , m_fullVideoActive(false)
#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    , m_composited(false)
#endif
{
    PRINTF_BLUE("%s MediaPlayerPrivate::MediaPlayerPrivate %p: %p", getTimeStamp(), this, player);
    m_element = static_cast<HTMLMediaElement*>(player->mediaPlayerClient());

    if (m_element)
        m_overlayplayback = !m_element->getAttribute("x-palm-media-extended-overlay-playback").isEmpty();
    m_remote.setListener(this);
    init();
}

void MediaPlayerPrivate::deinit()
{
    bool result = true;
    LSError lserror;
    LSErrorInit(&lserror);

    if (m_serverHandle) {
        result = LSUnregisterPalmService(m_serverHandle, &lserror);

        if (!result) {
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
        }
    }

    m_serverHandle = 0;

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    for (int i = 0; i < sizeof(m_textureId) / sizeof(m_textureId[0]); i++) {
        if (m_textureId[i]) {
            glDeleteTextures(1, &m_textureId[i]);
            m_textureId[i] = 0;
        }
    }
#endif
}

MediaPlayerPrivate::~MediaPlayerPrivate()
{
    PRINTF_BLUE("%s MediaPlayerPrivate::~MediaPlayerPrivate %p", getTimeStamp(), this);
    disconnect();
    deinit();
    if (m_videoFrameCaptured)
        ReleaseNativeSurface(m_videoFrameCaptured);
}

GMainLoop* MediaPlayerPrivate::s_mainLoop = 0;
void MediaPlayerPrivate::init()
{
#if PLATFORM(QT)
    // wraps qt main context in a dummy main loop
    if (!s_mainLoop) {
        GMainContext* ctxt = (GMainContext*) QAbstractEventDispatcher::instance()->platformHandle();
        s_mainLoop = g_main_loop_new(ctxt, TRUE);
    }
#endif
    bool result = true;
    LSError lserror;
    LSErrorInit(&lserror);

    if (!m_serverHandle) {
        result = LSRegisterPalmService(0, &m_serverHandle, &lserror);
        if (!result) {
            LSErrorPrint(&lserror, stderr);
            LSErrorFree(&lserror);
        } else {
            result = LSGmainAttachPalmService(m_serverHandle, s_mainLoop, &lserror);

            if (!result) {
                LSErrorPrint(&lserror, stderr);
                LSErrorFree(&lserror);
            }
        }
    }
#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    for (int i = 0; i < sizeof(m_textureId) / sizeof(m_textureId[0]); i++)
        m_textureId[i] = 0;
#endif
}

// Simple implementation of command line option splitting, not supporting '\'.
static int parseCommandlineString(char** str,  const char* commandline)
{
#define OUTPUT_PARAM \
        if (str) { \
            str[count] = (char*) g_malloc(i - paramStartIndex + 1); \
            memcpy(str[count], commandline + paramStartIndex, i - paramStartIndex); \
            str[count][i - paramStartIndex] = 0; \
        } \
        count++;

    int count = 0;
    bool inQuote = false;
    bool inSpace = true;
    int paramStartIndex = 0;
    for (int i = 0; ; ++i) {
        char c = commandline[i];
        if (!c)
            break;
        if (c == '"') {
            if (inQuote) {
                OUTPUT_PARAM
                inQuote = false;
            } else {
                paramStartIndex = i + 1;
                inQuote = true;
            }
        } else if (c == ' ') {
            if (!inQuote) {
                if (inSpace)
                    paramStartIndex = i + 1;
                else {
                    OUTPUT_PARAM
                    paramStartIndex = i + 1;
                    inSpace = true;
                }
            }
        } else {
            if (!inQuote && inSpace)
                inSpace = false;
        }
    }
    if (i > paramStartIndex) {
        // Just use the OUTPUT_PARAM macro.
        OUTPUT_PARAM
    }
    return count;
}
bool MediaPlayerPrivate::connectIfRequired()
{
    PRINTF_BLUE("%s MediaPlayerPrivate::connectIfRequired m_client %p m_capture %p", getTimeStamp(), m_client.get(), m_capture.get());
    if (m_client.get() || m_capture.get() || m_clonk.get())
        return true;

    if (m_lastLoadingUri == "") {
        PRINTF_RED("%s MediaPlayerPrivate::connectIfRequired no loading uri, quit", getTimeStamp());
        return false;
    }
    if (!m_lastLoadingUri.find("palm://com.palm.mediad.MediaCapture")) {
        PRINTF_YELLOW("%s connecting to existing capture %p", getTimeStamp(), this);
        m_capture = media::MediaClient::connectLunaMediaCapture(std::string(m_lastLoadingUri.utf8().data()), this);
        m_capturelistener = boost::shared_ptr<WebCore::MediaCaptureV3ChangeListener>(new WebCore::MediaCaptureV3ChangeListener(this));
        m_capture->addMediaCaptureV3ChangeListener(m_capturelistener);
        PRINTF_YELLOW("%s connected capture %p", getTimeStamp(), this);

        // TODO: should do following in MediaCaptureV3ChangeListener::readyChanged()
        m_bytesLoaded = 100000;
        m_duration = std::numeric_limits<float>::quiet_NaN();
        m_player->durationChanged();

        m_networkState = MediaPlayer::Loading;
        m_player->networkStateChanged();

        m_networkState = MediaPlayer::Loaded;
        m_player->networkStateChanged();

        m_readyState = MediaPlayer::HaveEnoughData;
        m_readyForDisplay = TRUE;
        m_player->readyStateChanged();
        m_player->repaint();
    } else if (!m_lastLoadingUri.find("palm://com.palm.mediad.Clonk")) {
        PRINTF_YELLOW("%s connecting to existing clonk %p", getTimeStamp(), this);
        m_clonk = media::MediaClient::connectLunaMediaClonk(std::string(m_lastLoadingUri.utf8().data()), this);
        m_clonklistener = boost::shared_ptr<WebCore::ClonkChangeListener>(new WebCore::ClonkChangeListener(this));
        m_clonk->addClonkChangeListener(m_clonklistener);

        PRINTF_YELLOW("%s connected clonk %p", getTimeStamp(), this);

        // FIXME: Get PiP info from clonk object and set it here?

        // FIXME: Take these out, maybe?
        m_bytesLoaded = 100000;
        m_duration = std::numeric_limits<float>::quiet_NaN();
        m_player->durationChanged();

        m_networkState = MediaPlayer::Loading;
        m_player->networkStateChanged();

        m_networkState = MediaPlayer::Loaded;
        m_player->networkStateChanged();

        m_readyState = MediaPlayer::HaveEnoughData;
        m_readyForDisplay = TRUE;
        m_player->readyStateChanged();
        m_player->repaint();

    } else if (!m_lastLoadingUri.find("palm://com.palm.mediad.MediaPlayer")) {
        PRINTF_YELLOW("%s connecting to existing player%p", getTimeStamp(), this);
        m_client = media::MediaClient::connectLunaMediaPlayer(std::string(m_lastLoadingUri.utf8().data()), this);
        m_listener = boost::shared_ptr<WebCore::MediaPlayerChangeListener>(new WebCore::MediaPlayerChangeListener(this));
        m_client->addMediaPlayerChangeListener(m_listener);
        PRINTF_YELLOW("%s connected player %p", getTimeStamp(), this);

    } else if (m_overlayplayback) {
        PRINTF_YELLOW("%s creating player %p", getTimeStamp(), this);
        m_client = media::MediaClient::createLunaMediaPlayer(*this);
        m_listener = boost::shared_ptr<WebCore::MediaPlayerChangeListener>(new WebCore::MediaPlayerChangeListener(this));
        m_client->addMediaPlayerChangeListener(m_listener);
        PRINTF_YELLOW("%s created player %p", getTimeStamp(), this);
    } else {

        if (m_remote.getPluginState() == RPS_NEVERLAUNCHED) {
            CString optionSettings = Settings::mediaPipelineOptions().utf8();
            int options = parseCommandlineString(0, optionSettings.data());
            char** argv = new char*[options+2];
            argv[0] = "MediaPlayer";
            argv[1] = "WebkitClient";
            if (options)
                parseCommandlineString(argv+2, optionSettings.data());
            m_remoteParams.m_mainloop = s_mainLoop;
            m_remoteParams.m_exePath = "/usr/bin/media-pipeline";
            m_remoteParams.m_argC = options+2;
            m_remoteParams.m_argV = argv;
            m_remoteParams.m_portalWidth = -1;
            m_remoteParams.m_portalHeight = -1;
            if (m_width>0 && m_height>0) {
                m_remoteParams.m_portalWidth = m_width;
                m_remoteParams.m_portalHeight = m_height;
            }
            PRINTF_YELLOW("%s creating player %p", getTimeStamp(), this);
            if (m_remote.launchPlugin(&m_remoteParams)) {
                m_client = media::MediaClient::connectLunaMediaPlayer(media::Media::getUri(media::Media::MEDIA_PLAYER_INTERFACE, m_remote.getRemotePluginPID()), this);
                m_listener = boost::shared_ptr<WebCore::MediaPlayerChangeListener>(new WebCore::MediaPlayerChangeListener(this));
                m_client->addMediaPlayerChangeListener(m_listener);
                PRINTF_YELLOW("%s created player %p", getTimeStamp(), this);
            }
            for (int i = 0; i < options; ++i)
                g_free(argv[i+2]);
            delete []argv;
        }

    }
    return true;
}

void MediaPlayerPrivate::setFitMode(const String& mode)
{
    if (m_client) {
        media::VideoFitMode videoFitMode;
        if (mode == "VIDEO_FILL")
            videoFitMode = media::VIDEO_FILL;
        else if (mode == "VIDEO_FIT")
            videoFitMode = media::VIDEO_FIT;
        else
            return;
        // change immediately so naturalSize will return the corrected size
        m_fitMode = videoFitMode;
        m_client->setFitMode(videoFitMode);
    }
}

void MediaPlayerPrivate::update()
{
#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    if (supportsAcceleratedRendering() && m_composited) {
        PRINTF_BLUE("graphicsLayer()->SetNeedsDisplay");
        graphicsLayer()->setNeedsDisplay();
    } else
#endif
    {
        m_player->repaint();
        // Swap Buffer
        if (lockBuffer(&m_currentRemoteBufferHold, true))
            unlockBuffer();
    }
}

void MediaPlayerPrivate::draw()
{
    m_updatesPending++;

    update();
}

void MediaPlayerPrivate::captureRemoteBuffer()
{
    if (!m_overlayplayback) {
#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    if (supportsAcceleratedRendering() && m_composited) {
        RemotePluginBuffer remoteBuffer;
        if (lockBuffer(&remoteBuffer, true)) {
            char* frameData = (char*)getVideoFrameCaptureData(remoteBuffer.m_bufferWidth, remoteBuffer.m_bufferHeight);
            char* remoteBufferData = (char*)remoteBuffer.m_pixels;
            for (int i = 0; i < remoteBuffer.m_bufferHeight; ++i) {
                memcpy(frameData, remoteBufferData, remoteBuffer.m_bufferWidth*4);
                frameData += 4*remoteBuffer.m_bufferWidth;
                remoteBufferData += remoteBuffer.m_bufferPitch;
            }
            unlockBuffer();
        }
    } else
#endif
        if (m_currentRemoteBuffer >= 0) {
            char* frameData = (char*)getVideoFrameCaptureData(m_currentRemoteBufferHold.m_bufferWidth, m_currentRemoteBufferHold.m_bufferHeight);
            char* remoteBufferData = (char*)m_currentRemoteBufferHold.m_pixels;
            for (int i = 0; i < m_currentRemoteBufferHold.m_bufferHeight; ++i) {
                memcpy(frameData, remoteBufferData, m_currentRemoteBufferHold.m_bufferWidth*4);
                frameData += 4*m_currentRemoteBufferHold.m_bufferWidth;
                remoteBufferData += m_currentRemoteBufferHold.m_bufferPitch;
            }
        }
    }
}

void MediaPlayerPrivate::pageFocus(bool focus)
{
    PRINTF_YELLOW("MediaPlayerPrivate::pageFocus %d", focus);
    if (m_client.get()) {
        // Only care about player
        if (!focus) {
            if (!m_overlayplayback) {
                captureRemoteBuffer();
                m_client->suspend();
            } else {
                if (m_paused)
                    m_client->suspend();
                else
                    m_unloading = true;
            }
        }
    }
}

void MediaPlayerPrivate::disconnect()
{
    PRINTF_BLUE("%s MediaPlayerPrivate::disconnect %p", getTimeStamp(), this);
    releaseBuffer();
    if (m_client.get()) {
        PRINTF_YELLOW("Resetting player");
        m_client->removeMediaPlayerChangeListener(m_listener);
        m_client.reset((media::MediaPlayer*)0);
    }
    m_remote.terminatePlugin();
    if (m_capture.get()) {
        PRINTF_YELLOW("Resetting capture");
        m_capture->removeMediaCaptureV3ChangeListener(m_capturelistener);
        m_capture.reset((media::MediaCaptureV3*)0);
    }
    if (m_clonk.get()) {
        PRINTF_YELLOW("Resetting clonk");
        m_clonk->removeClonkChangeListener(m_clonklistener);
        m_clonk.reset((media::Clonk*)0);
    }

    m_paused = true;
    m_seeking = false;
    m_currentTime = 0;
    m_currentPlayer = "";
    m_loadPendingUri = false;
    m_bytesLoaded = 0;
    m_unloading = false;
}

void MediaPlayerPrivate::load(const String& url)
{
    PRINTF_BLUE("%s MediaPlayerPrivate::load %p %s", getTimeStamp(), this, url.utf8().data());
    m_lastLoadingUri = url;
    // create stub if not created
    if (!m_client.get() && !m_capture.get() && !m_clonk.get()) {
        m_loadPendingUri = true;
        PRINTF_BLUE("%s MediaPlayerPrivate::load call connectIfRequired", getTimeStamp());
        connectIfRequired();
        return;
    }

    // stub was created, we only load for player
    if (!m_client.get())
        return;
    // check if player pieline is created
    if (m_currentPlayer == "") {
        PRINTF_BLUE("%s MediaPlayerPrivate::load pipeline not ready yet", getTimeStamp());
        // pipeline not created yet
        m_loadPendingUri = true;
        return;
    }
    media::AudioStreamClass audioClass = media::kAudioStreamDefaultapp;

    if (m_element) {
        const String& ac = m_element->getAttribute("x-palm-media-audio-class");
        if (!ac.isEmpty()) {
            string aClass = string(ac.utf8().data());
            if (aClass == "ringtones")
                audioClass = media::kAudioStreamRingtone;
            else if (aClass == "alerts")
                audioClass = media::kAudioStreamAlert;
            else if (aClass == "media")
                audioClass = media::kAudioStreamMedia;
            else if (aClass == "notifications")
                audioClass = media::kAudioStreamNotification;
            else if (aClass == "feedback")
                audioClass = media::kAudioStreamFeedback;
            else if (aClass == "flash" || aClass == "games")
                audioClass = media::kAudioStreamFlash;
            else if (aClass == "navigation")
                audioClass = media::kAudioStreamNavigation;
            else if (aClass == "voicedial")
                audioClass = media::kAudioStreamVoicedial;
            else if (aClass == "calendar")
                audioClass = media::kAudioStreamCalendar;
            else if (aClass == "alarm")
                audioClass = media::kAudioStreamAlarm;
            else if (aClass == "vvm")
                audioClass = media::kAudioStreamVvm;
            else
                audioClass = media::kAudioStreamDefaultapp;
        }
        PRINTF_BLUE("MediaPlayerPrivate::load: audio class is '%s'", ac.utf8().data());
    }

    boost::shared_ptr<media::HttpParameters> httpPara(new media::HttpParameters());
    KURL kurl(ParsedURLString, url);
    Document* document = m_element? m_element->document() : 0;
    Frame* frame = document? document->frame() : 0;
    FrameLoader* loader = frame ? frame->loader() : 0;
    FrameLoaderClient* loaderclient = loader? loader->client() : 0;
    if (document)
        httpPara->cookies = cookies(document, kurl).utf8().data();
    if (loaderclient)
        httpPara->useragent = loaderclient->userAgent(kurl).utf8().data();
    INVOKE_PLAYER(setHttpparameters, httpPara);

    string uri = string(url.utf8().data());

    INVOKE_PLAYER(load, uri, audioClass);

    m_networkState = MediaPlayer::Loading;
    m_player->networkStateChanged();

    m_loadPendingUri = false;
}

void MediaPlayerPrivate::play()
{
    PRINTF_BLUE("%s MediaPlayerPrivate::play %p", getTimeStamp(), this);
    // const String& ext = m_element->getAttribute("x-palm-media-extension");

    if (m_client.get()) {
        m_paused = false;
        INVOKE_PLAYER(play);
    }

    VERBOSE_TRACE("Play");
}

void MediaPlayerPrivate::pause()
{
    PRINTF_BLUE("%s MediaPlayerPrivate::pause %p", getTimeStamp(), this);
    // const String& ext = m_element->getAttribute("x-palm-media-extension");

    if (m_client.get()) {
        m_paused = true;
        INVOKE_PLAYER(pause);
    }

    VERBOSE_TRACE("Pause");
}

float MediaPlayerPrivate::duration() const
{
    return m_duration;
}

float MediaPlayerPrivate::currentTime() const
{
    return m_currentTime;
}

void MediaPlayerPrivate::seek(float time)
{
    PRINTF_BLUE("%s MediaPlayerPrivate::seek %f", getTimeStamp(), time);
    const String& ext = m_element->getAttribute("x-palm-media-extension");
    if (m_capture.get()) {
        m_currentTime = time;
        m_player->timeChanged();
        return;
    }

    if (m_client.get())
        INVOKE_PLAYER(seek, time);
}

void MediaPlayerPrivate::setEndTime(float time)
{
    notImplemented();
}

bool MediaPlayerPrivate::paused() const
{
    return m_paused;
}

bool MediaPlayerPrivate::seeking() const
{
    return m_seeking;
}

// Returns the size of the video
IntSize MediaPlayerPrivate::naturalSize() const
{
    if (!m_overlayplayback && m_client && m_fitMode == media::VIDEO_FIT) {
        int videoWidth = m_client->getVideoWidth();
        int videoHeight = m_client->getVideoHeight();
        if (videoWidth>0 && videoHeight>0)
            return IntSize(videoWidth, videoHeight);
        return IntSize(0, 0);
    }
    int width = m_element->clientWidth();
    int height = m_element->clientHeight();
    return IntSize(width, height);
}

bool MediaPlayerPrivate::hasVideo() const
{
    return m_hasVideo;
}

bool MediaPlayerPrivate::hasAudio() const
{
    return m_hasAudio;
}

void MediaPlayerPrivate::setVolume(float volume)
{
    notImplemented();
}

void MediaPlayerPrivate::setRate(float rate)
{
    notImplemented();
}

int MediaPlayerPrivate::dataRate() const
{
    notImplemented();
    return 1;
}

MediaPlayer::NetworkState MediaPlayerPrivate::networkState() const
{
    return m_networkState;
}

static const char* getReadyStateName(MediaPlayer::ReadyState state)
{
    const char* name = "??";
    switch (state) {
    case MediaPlayer::HaveNothing:
        name = "nothing";
        break;
    case MediaPlayer::HaveMetadata:
        name = "meta data";
        break;
    case MediaPlayer::HaveCurrentData:
        name = "current data";
        break;
    case MediaPlayer::HaveFutureData:
        name = "future data";
        break;
    case MediaPlayer::HaveEnoughData:
        name = "enough data";
        break;
    }
    return name;
}

MediaPlayer::ReadyState MediaPlayerPrivate::readyState() const
{
    PRINTF_BLUE("%s MediaPlayerPrivate::readyState %p - returning %s", getTimeStamp(), this, getReadyStateName(m_readyState));
    return m_readyState;
}

PassRefPtr<TimeRanges> MediaPlayerPrivate::buffered() const
{
    RefPtr<TimeRanges> timeRanges = TimeRanges::create();

    float bytesloaded = bytesLoaded();
    float totalbytes = totalBytes();
    float duration = maxTimeSeekable();
    float loaded = 0;

    if (totalbytes)
        loaded = (bytesloaded * duration) / totalbytes;

    timeRanges->add(0, loaded);
    return timeRanges.release();
}

float MediaPlayerPrivate::maxTimeSeekable() const
{
    return duration();
}

unsigned MediaPlayerPrivate::bytesLoaded() const
{
    return m_bytesLoaded;
}

unsigned MediaPlayerPrivate::totalBytes() const
{
    return m_totalBytes;
}

void MediaPlayerPrivate::cancelLoad()
{
    PRINTF_BLUE("%s MediaPlayerPrivate::cancelLoad %p", getTimeStamp(), this);
    notImplemented();
}

void MediaPlayerPrivate::setSize(const IntSize& size)
{
    PRINTF_BLUE("setSize %d %d\n", size.width(), size.height());
    if (!m_client || m_overlayplayback) {
        m_width  = m_element->clientWidth();
        m_height = m_element->clientHeight();
        return;
    }
    bool sendChange = false;
    if (m_width != size.width() || m_height != size.height()
#if defined(MEDIAPLAYER_PALM_SCALE_REMOTE_VIDEO)
        || m_lastGcScale != m_remoteBufferScale
#endif
    ) {
        m_width = size.width();
        m_height = size.height();
        m_remoteBufferScale = m_lastGcScale;
        if (m_remote.getPluginState() == RPS_CONNECTED)
            sendChange = true;
        else
            m_remoteBufferResizePending = true;
        PRINTF_BLUE("%s MediaPlayerPrivate::setSize %p %d-%d -> %d-%d", getTimeStamp(), this, size.width(), size.height(), m_width, m_height);
    } else if (m_remoteBufferResizePending && m_remote.getPluginState() == RPS_CONNECTED)
        sendChange = true;
    if (sendChange) {
        int resolutionWidth = m_width;
        int resolutionHeight = m_height;
        if (!resolutionWidth || !resolutionHeight)
            return;
#if defined(MEDIAPLAYER_PALM_SCALE_REMOTE_VIDEO)
        resolutionWidth *= m_remoteBufferScale;
        resolutionHeight *= m_remoteBufferScale;
#endif
        PRINTF_YELLOW("%s resize %p plugin -> %d-%d", getTimeStamp(), this, resolutionWidth, resolutionHeight);
        releaseBuffer();
        m_remoteBufferResizePending = !m_remote.sendEventResolutionChange(resolutionWidth, resolutionHeight);
    }
}

void MediaPlayerPrivate::setVisible(bool visible)
{
    // PRINTF_BLUE("%s MediaPlayerPrivate::setVisible %p - %d", getTimeStamp(), this, int(visible));
    notImplemented();
}

void MediaPlayerPrivate::renderFrame(GraphicsContext* context, WebOSNativeSurface* src, const IntRect& rect)
{
    {
        // keep the aspect ratio:
        // when we changing the fitmode, there can be a small time window that inconsistency exists between the remote
        // buffer size and webkit intrinsic size, so we must ensure the aspect isn't changed in bitblt
        if (!rect.height() || !rect.width() || !src->width() || !src->height())
            return;
        float rectAspect = (float)rect.width() / (float)rect.height();
        float bufferAspect = (float)src->width() / (float)src->height();
        switch (m_fitMode) {
        case media::VIDEO_FIT: {
            IntRect adjustedRect(rect);
            if (bufferAspect>rectAspect) {
                int adjustedRectHeight = rect.width() / bufferAspect;
                adjustedRect.setY(rect.y() + (rect.height()-adjustedRectHeight) / 2);
                adjustedRect.setHeight(adjustedRectHeight);
            } else if (bufferAspect < rectAspect) {
                int adjustedRectWidth = rect.height() * bufferAspect;
                adjustedRect.setX(rect.x() + (rect.width()-adjustedRectWidth) / 2);
                adjustedRect.setWidth(adjustedRectWidth);
            }
            DrawNativeSurface(context, src, 0, 0, src->width(), src->height(), adjustedRect.x(), adjustedRect.y(), adjustedRect.width(), adjustedRect.height());
        }
        break;
        case media::VIDEO_FILL: {
            IntRect adjustedRect(0, 0, src->width(), src->height());
            if (bufferAspect > rectAspect) {
                int adjustedRectWidth = src->height() * rectAspect;
                adjustedRect.setX((src->width()-adjustedRectWidth) / 2);
                adjustedRect.setWidth(adjustedRectWidth);
            } else if (bufferAspect < rectAspect) {
                int adjustedRectHeight = src->width() / rectAspect;
                adjustedRect.setY((src->height()-adjustedRectHeight) / 2);
                adjustedRect.setHeight(adjustedRectHeight);
            }
            DrawNativeSurface(context, src, adjustedRect.x(), adjustedRect.y(), adjustedRect.width(), adjustedRect.height(), rect.x(), rect.y(), rect.width(), rect.height());
        }
        break;
        default:
            DrawNativeSurface(context, src, 0, 0, src->width(), src->height(), rect.x(), rect.y(), rect.width(), rect.height());
            break;
        }
    }
}

void MediaPlayerPrivate::paint(GraphicsContext* context, const IntRect& rect)
{
    if (context->paintingDisabled())
        return;
    if (!m_readyForDisplay) {
        PRINTF_RED("%s MediaPlayerPrivate::paint %p NOT READY FOR DISPLAY! (%d, %d) %dx%d", getTimeStamp(), this, rect.x(), rect.y(), rect.width(), rect.height());
        return;
    }

    if (m_clonk) {
        if (m_fullVideoActive) {
            // Regardless of whether the pipvideo is active, paint whole screen
            PRINTF_BLUE("~~ whole screen active -- painting everything ~~");
            context->setFillColor(Color::transparent, ColorSpaceDeviceRGB);
            context->clearRect(rect);
        } else if (m_pipVideoActive) {
            // Then just fill the PiP area
            PRINTF_BLUE("~~ painting PiP only ~~");
            // Temp workaround since PIP dimensions may not be correctly translated
            // c->clearRect(m_pipRect.x(), m_pipRect.y(), m_pipRect.right(), m_pipRect.bottom());
            context->setFillColor(Color::transparent, ColorSpaceDeviceRGB);
            context->clearRect(rect);
        }

        // Otherwise, neither are active. Do nothing.
    } else if (m_client && m_overlayplayback) {
        // overlay playback
        if (!m_videoFrameCaptured) {
            PRINTF_BLUE("%s MediaPlayerPrivate::paint %p overlay (%d, %d) %dx%d", getTimeStamp(), this, rect.x(), rect.y(), rect.width(), rect.height());
            context->setFillColor(Color::transparent, ColorSpaceDeviceRGB);
            context->clearRect(rect);
        } else {
           PRINTF_BLUE("%s MediaPlayerPrivate::paint %p (%d, %d) %dx%d from %dx%d capture", getTimeStamp(), this, rect.x(), rect.y(), rect.width(), rect.height(),
                          m_videoFrameCaptured->width(), m_videoFrameCaptured->height());
           renderFrame(context, m_videoFrameCaptured, rect);
        }
    } else if (m_client && !m_overlayplayback) {
        PRINTF_BLUE("%s MediaPlayerPrivate::paint %p (%d, %d) %dx%d", getTimeStamp(), this, rect.x(), rect.y(), rect.width(), rect.height());
        // libremoteplugin playback
#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
        if (m_composited) {
            if (m_videoFrameCaptured)
                renderFrame(context, m_videoFrameCaptured, rect);
            return;
        }
#endif
        if (m_videoFrameCaptured) {
            WebOSNativeSurface* frame = m_videoFrameCaptured;
            if (m_currentRemoteBuffer >= 0) {
                if (!m_paused && (m_currentRemoteBufferHold.m_bufferWidth != m_videoFrameCaptured->width() || m_currentRemoteBufferHold.m_bufferHeight != m_videoFrameCaptured->height()))
                    m_videoFrameCaptured = 0;
            }
            renderFrame(context, frame, rect);
            if (!m_videoFrameCaptured) {
                // release the frame captured in resize video tag
                ReleaseNativeSurface(frame);
            }
        } else if (m_currentRemoteBuffer >= 0) {
            WebOSNativeSurface* src = WrapNativeSurface(m_currentRemoteBufferHold.m_pixels, m_currentRemoteBufferHold.m_bufferWidth, m_currentRemoteBufferHold.m_bufferHeight, m_currentRemoteBufferHold.m_bufferPitch);
            renderFrame(context, src, rect);
            ReleaseNativeSurface(src);
        } else {
            PRINTF_BLUE("%s MediaPlayerPrivate::paint %p overlay (%d, %d) %dx%d", getTimeStamp(), this, rect.x(), rect.y(), rect.width(), rect.height());
            context->setFillColor(Color::black, ColorSpaceDeviceRGB);
            context->clearRect(rect);
        }
    } else {
        PRINTF_BLUE("%s MediaPlayerPrivate::paint %p overlay (%d, %d) %dx%d", getTimeStamp(), this, rect.x(), rect.y(), rect.width(), rect.height());
        context->setFillColor(Color::transparent, ColorSpaceDeviceRGB);
        context->clearRect(rect);
    }

}

PassOwnPtr<MediaPlayerPrivateInterface> MediaPlayerPrivate::create(MediaPlayer* player)
{
    return adoptPtr(new MediaPlayerPrivate(player));
}

void MediaPlayerPrivate::registerMediaEngine(MediaEngineRegistrar registrar)
{
    if (isAvailable())
        registrar(create, getSupportedTypes, supportsType, 0, 0, 0);
}

HashSet<String> & MediaPlayerPrivate::GetTypeCache()
{
    static HashSet<String> typeCache;
    static bool typeCacheInitialized = false;
    if (!typeCacheInitialized) {
        typeCache.add(String("video/mp4"));
        typeCache.add(String("video/x-m4v"));
        typeCache.add(String("audio/x-m4a"));
        typeCache.add(String("audio/3gpp2"));
        typeCache.add(String("audio/aac"));
        typeCache.add(String("audio/wav"));
        typeCache.add(String("audio/mp3"));
        typeCache.add(String("application/vnd.apple.mpegurl"));
        typeCache.add(String("application/x-mpegurl"));
        typeCache.add(String("audio/x-mpegurl"));
        typeCache.add(String("audio/ogg"));
        typeCache.add(String("audio/flac"));
        typeCache.add(String("video/x-ms-asf"));
        typeCache.add(String("audio/x-ms-wma"));
        typeCache.add(String("audio/x-ms-wmv"));
        typeCache.add(String("video/x-ms-wmv"));
        typeCache.add(String("video/avi"));
        typeCache.add(String("video/msvideo"));
        typeCache.add(String("video/x-msvideo"));
        typeCacheInitialized = true;
    }
    return typeCache;
}

void MediaPlayerPrivate::getSupportedTypes(HashSet<String>& types)
{
    types = GetTypeCache();
}

MediaPlayer::SupportsType MediaPlayerPrivate::supportsType(const String& type, const String& codecs)
{
    if (type.isNull() || type.isEmpty())
        return MediaPlayer::IsNotSupported;

    if (GetTypeCache().contains(type))
        return codecs.isEmpty() ? MediaPlayer::MayBeSupported : MediaPlayer::IsSupported;
    return MediaPlayer::IsNotSupported;
}

int* MediaPlayerPrivate::getVideoFrameCaptureData(int width, int height)
{
    if (m_videoFrameCaptured && (width != (int) m_videoFrameCaptured->width() || height != (int) m_videoFrameCaptured->height())) {
        ReleaseNativeSurface(m_videoFrameCaptured);
        m_videoFrameCaptured = 0;
    }
    if (!m_videoFrameCaptured)
        m_videoFrameCaptured = CreateNativeSurface(width, height);
    int * videoFrameData = (int *) (m_videoFrameCaptured ? GetNativeSurfaceBits(m_videoFrameCaptured) : 0);
    VERIFY(videoFrameData);
    if (!videoFrameData) {
        if (m_videoFrameCaptured)
            ReleaseNativeSurface(m_videoFrameCaptured);
        m_videoFrameCaptured = 0;
    }
    return videoFrameData;
}

LSPalmService* MediaPlayerPrivate::connectToBus()
{
    PRINTF_BLUE("%s MediaPlayerPrivate::connectToBus %p c:%p ls:%p", getTimeStamp(), this, m_client.get(), m_serverHandle);

    return m_serverHandle;
}

void MediaPlayerPrivate::connected()
{
    string location;
    bool isCapture;
    if (m_client.get()) {
        location = m_client->_getObjectUri();
        isCapture = false;
    } else if (m_capture.get()) {
        location = m_capture->_getObjectUri();
        isCapture = true;
    } else {
        PRINTF_RED("%s MediaPlayerPrivate::connected reset been called?", getTimeStamp());
        return;
    }
    PRINTF_BLUE("%s MediaPlayerPrivate::connected %p %s", getTimeStamp(), this, location.c_str());

    if (location.size() > 0) {
        m_currentPlayer = location;
        if (m_element) {
            ExceptionCode ec;
            m_element->setAttribute("x-palm-media-control", m_currentPlayer.c_str(), ec);
            m_element->setAttribute("x-palm-media-pausable", "true", ec);
        }

        if (!isCapture && m_loadPendingUri) {
            PRINTF_BLUE("connected() loading pending uri");
            load(m_lastLoadingUri);
        }
    }

    if (!m_client.get() || m_overlayplayback) {
        m_width = m_element->clientWidth();
        m_height = m_element->clientHeight();
    }
}

Image* MediaPlayerPrivate::getPlayButtonImage()
{
    static RefPtr<Image> s_playButtonImage;

    if (!s_playButtonImage)
        s_playButtonImage = Image::loadPlatformResource(s_playButtonImgPath);

    return s_playButtonImage.get();
}

void MediaPlayerPrivate::releaseBuffer()
{
    if (m_currentRemoteBuffer >= 0) {
        m_remote.finishedDrawing(m_currentRemoteBuffer);
        m_currentRemoteBuffer = -1;
    }
}

bool MediaPlayerPrivate::lockBuffer(RemotePluginBuffer* buffer, bool invalidateCache)
{
    if (m_remote.lockBuffer(buffer, invalidateCache)) {
        if (m_currentRemoteBuffer >= 0) {
            if (buffer->m_index != m_currentRemoteBuffer) {
                // switching buffers, release the current (old) one
                m_remote.finishedDrawing(m_currentRemoteBuffer);
            }
        }
        m_currentRemoteBuffer = buffer->m_index;

        return true;
    }

    return false;
}

void MediaPlayerPrivate::unlockBuffer()
{
    m_remote.unlockBuffer(m_currentRemoteBuffer);

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    if (supportsAcceleratedRendering() && m_composited) {
        if (m_updatesPending) {
            m_updatesPending--;
            if (m_updatesPending)
                update();
        }
    }
#endif
}

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)

bool MediaPlayerPrivate::supportsAcceleratedRendering() const
{
    return !m_overlayplayback && !m_videoFrameCaptured && m_client;
}

void MediaPlayerPrivate::acceleratedRenderingStateChanged()
{
    bool composited = m_player->mediaPlayerClient()->mediaPlayerRenderingCanBeAccelerated(m_player);
    if (composited == m_composited)
        return;

    releaseBuffer();
    m_composited = composited;
    if (m_composited) {
        if (m_videoFrameCaptured) {
            ReleaseNativeSurface(m_videoFrameCaptured);
            m_videoFrameCaptured = 0;
        }
    }
}

PlatformLayer* MediaPlayerPrivate::platformLayer() const
{
    return const_cast<MediaPlayerPrivate*>(this);
}

void MediaPlayerPrivate::paintToTextureMapper(TextureMapper* textureMapper, const FloatRect& targetRect, const TransformationMatrix& matrix, float opacity, BitmapTexture* mask) const
{
    TextureMapperGL* texmapGL = dynamic_cast<TextureMapperGL*>(textureMapper);
    if (texmapGL && texmapGL->isOpenGLBacked()) {
        RemotePluginBuffer remoteBuffer;
        if (lockBuffer(&remoteBuffer, false)) {
            if (!m_textureId[m_currentRemoteBuffer]) {
                glGenTextures(1, &m_textureId[m_currentRemoteBuffer]);
                m_remote.mapBufferTexture(m_currentRemoteBuffer, m_textureId[m_currentRemoteBuffer]);
            }
            if (m_textureId[m_currentRemoteBuffer])
                texmapGL->drawTexture(m_textureId[m_currentRemoteBuffer], true, FloatSize(1, 1), targetRect, matrix, opacity, mask, false /* flip */);
            unlockBuffer();
        }
        return;
    }
    GraphicsContext* context = textureMapper->graphicsContext();
    QPainter* painter = context->platformContext();
    painter->save();
    painter->setTransform(matrix);
    painter->setOpacity(opacity);
    RemotePluginBuffer remoteBuffer;
    if (lockBuffer(&remoteBuffer, true)) {
        WebOSNativeSurface* src = WrapNativeSurface(remoteBuffer.m_pixels, remoteBuffer.m_bufferWidth, remoteBuffer.m_bufferHeight, remoteBuffer.m_bufferPitch);
        renderFrame(context, src, (IntRect)targetRect);
        ReleaseNativeSurface(src);
        unlockBuffer();
    }
    painter->restore();
}
#endif

} // namespace WebCore

#endif // ENABLE(VIDEO)
