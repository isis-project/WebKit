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

#ifndef MediaPlayerPrivateWebos_h
#define MediaPlayerPrivateWebos_h

#if ENABLE(VIDEO)

#include "HTMLMediaElement.h"
#include "Image.h"
#include "MediaPlayerPrivate.h"
#include "RefPtr.h"
#include "TimeRanges.h"

#include <lunaservice.h>
#include <media/Clonk.h>
#include <media/LunaConnector.h>
#include <media/MediaCaptureV3.h>
#include <media/MediaClient.h>
#include <media/MediaPlayer.h>
#include <pbnjson.hpp>
#include <remoteplugin/RemotePlugin.h>

#if PLATFORM(QT)
#define WebOSNativeSurface QImage
#else
#error WebOSNativeSurface not defined
#endif

class WebOSNativeSurface;

namespace WTF {
class String;
}

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
#include "TextureMapper.h"
#endif

namespace WebCore {

class MediaChangeListener;
class MediaPlayerChangeListener;
class MediaCaptureV3ChangeListener;
class ClonkChangeListener;


class IntSize;
class IntRect;

class MediaPlayerPrivate : public MediaPlayerPrivateInterface, media::LunaConnector, RemotePluginListener
#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
        , public TextureMapperPlatformLayer
#endif
{
    friend class WebCore::MediaChangeListener;
    friend class WebCore::MediaPlayerChangeListener;
    friend class WebCore::MediaCaptureV3ChangeListener;
    friend class WebCore::ClonkChangeListener;

public:
    static void registerMediaEngine(MediaEngineRegistrar);
    virtual ~MediaPlayerPrivate();

    virtual void load(const String& url);
    virtual void cancelLoad();
    virtual void play();
    virtual void pause();
    virtual void disconnect();
    virtual void pageFocus(bool focus);
    virtual void setFitMode(const String&);

    virtual IntSize naturalSize() const;

    virtual bool hasVideo() const;
    virtual bool hasAudio() const;

    virtual void setVisible(bool);
    virtual float duration() const;

    virtual float currentTime() const;
    virtual void seek(float time);
    virtual bool seeking() const;

    virtual void setEndTime(float);
    virtual void setRate(float);

    virtual bool paused() const;
    virtual void setVolume(float);

    virtual MediaPlayer::NetworkState networkState() const;
    virtual MediaPlayer::ReadyState readyState() const;

    virtual float maxTimeSeekable() const;
    virtual PassRefPtr<TimeRanges> buffered() const;

    virtual int dataRate() const;

    virtual unsigned totalBytes() const;
    virtual unsigned bytesLoaded() const;

    virtual void setSize(const IntSize&);
    virtual void paint(GraphicsContext*, const IntRect&);

    void releaseBuffer();

    bool hasSingleSecurityOrigin() const { return true; }

private:
    MediaPlayerPrivate(MediaPlayer*);
    static PassOwnPtr<MediaPlayerPrivateInterface> create(MediaPlayer*);

    static void getSupportedTypes(HashSet<String>&);
    static MediaPlayer::SupportsType supportsType(const String& type, const String& codecs);
    static bool isAvailable() { return true; }

    static HashSet<String> & GetTypeCache();

    virtual void draw();
    bool connectIfRequired();
    void init();
    void deinit();

    void update();
    bool lockBuffer(RemotePluginBuffer*, bool invalidateCache);
    void unlockBuffer();
    void captureRemoteBuffer();

    void renderFrame(GraphicsContext*, WebOSNativeSurface* src, const IntRect&);

    Image* getPlayButtonImage();

    int* getVideoFrameCaptureData(int width, int height);

    // MediaConnector methods
    virtual LSPalmService* connectToBus();
    virtual void connected();

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    virtual bool supportsAcceleratedRendering() const;
    virtual void acceleratedRenderingStateChanged();
    virtual PlatformLayer* platformLayer() const;
    void paintToTextureMapper(TextureMapper*, const FloatRect& targetRect, const TransformationMatrix&, float opacity, BitmapTexture*) const;
#endif

private:
    MediaPlayer* m_player;
    RemotePlugin m_remote;
    RemotePluginParams m_remoteParams;
    int m_currentRemoteBuffer;
    RemotePluginBuffer m_currentRemoteBufferHold;
    int m_updatesPending;
    float m_remoteBufferScale;
    float m_lastGcScale;
    bool m_remoteBufferResizePending;
    HTMLMediaElement* m_element;

    WebOSNativeSurface* m_videoFrameCaptured;

    MediaPlayer::NetworkState m_networkState;
    MediaPlayer::ReadyState m_readyState;
    bool m_readyForDisplay;

    boost::shared_ptr<media::MediaPlayer> m_client;
    boost::shared_ptr<WebCore::MediaPlayerChangeListener> m_listener;

    boost::shared_ptr<media::MediaCaptureV3> m_capture;
    boost::shared_ptr<WebCore::MediaCaptureV3ChangeListener> m_capturelistener;

    boost::shared_ptr<media::Clonk> m_clonk;
    boost::shared_ptr<WebCore::ClonkChangeListener> m_clonklistener;

    LSPalmService* m_serverHandle;

    int m_bytesLoaded;
    int m_totalBytes;
    float m_currentTime;
    float m_duration;
    bool m_paused;
    bool m_seeking;
    bool m_hasAudio;
    bool m_hasVideo;
    bool m_unloading;

    HashSet<String> typeCache;
    String m_lastLoadingUri;
    bool m_loadPendingUri;
    std::string m_currentPlayer;

    bool m_overlayplayback;
    int m_width;
    int m_height;
    media::VideoFitMode m_fitMode;

    bool m_pausable;

    // picture-in-picture related member variables
    IntRect m_pipRect;
    bool m_pipVideoActive;
    bool m_fullVideoActive;

    static GMainLoop* s_mainLoop;

#if USE(ACCELERATED_COMPOSITING) && USE(TEXTURE_MAPPER)
    bool m_composited;
    int m_textureId[2];
#endif
};
} // namespace WebCore

#endif // ENABLE(VIDEO)
#endif // MediaPlayerPrivateWebos_h
