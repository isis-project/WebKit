/*
    Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies)
    Copyright (C) 2011 Hewlett-Packard Development Company, L.P.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#ifndef TextureMapperPlatformLayer_h
#define TextureMapperPlatformLayer_h

#include "FloatRect.h"
#if ENABLE(VIDEO) && USE(WEBOS_MULTIMEDIA)
#include "GraphicsLayer.h"
#endif
#include "TransformationMatrix.h"

namespace WebCore {

class TextureMapper;
class BitmapTexture;

class TextureMapperPlatformLayer {
public:
    virtual void paintToTextureMapper(TextureMapper*, const FloatRect&, const TransformationMatrix& modelViewMatrix = TransformationMatrix(), float opacity = 1.0, BitmapTexture* mask = 0) const = 0;
#if ENABLE(VIDEO) && USE(WEBOS_MULTIMEDIA)
    // for accessing GraphicsLayer::setNeedsDisplay from platform layer
    TextureMapperPlatformLayer():m_layer(0) { }
    void setGraphicsLayer(GraphicsLayer* layer) { m_layer = layer; }
    GraphicsLayer* graphicsLayer() { return m_layer; }
protected:
    GraphicsLayer* m_layer;
#endif
};

};

#endif // TextureMapperPlatformLayer_h
