/*
 * Copyright (C) 2004, 2005, 2006, 2007, 2008 Nikolas Zimmermann <zimmermann@kde.org>
 * Copyright (C) 2004, 2005, 2006, 2008 Rob Buis <buis@kde.org>
 * Copyright (C) 2008 Apple Inc. All rights reserved.
 * Copyright (C) 2008 Alp Toker <alp@atoker.com>
 * Copyright (C) 2009 Cameron McCormack <cam@mcc.id.au>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#if ENABLE(SVG)
#include "SVGElement.h"

#include "CSSCursorImageValue.h"
#include "DOMImplementation.h"
#include "Document.h"
#include "Event.h"
#include "HTMLNames.h"
#include "NodeRenderingContext.h"
#include "RenderObject.h"
#include "SVGCursorElement.h"
#include "SVGDocumentExtensions.h"
#include "SVGElementInstance.h"
#include "SVGElementRareData.h"
#include "SVGNames.h"
#include "SVGSVGElement.h"
#include "SVGStyledLocatableElement.h"
#include "SVGTextElement.h"
#include "ScriptEventListener.h"
#include "XMLNames.h"

namespace WebCore {

using namespace HTMLNames;

SVGElement::SVGElement(const QualifiedName& tagName, Document* document, ConstructionType constructionType)
    : StyledElement(tagName, document, constructionType)
{
    setHasCustomStyleForRenderer();
    setHasCustomWillOrDidRecalcStyle();
}

PassRefPtr<SVGElement> SVGElement::create(const QualifiedName& tagName, Document* document)
{
    return adoptRef(new SVGElement(tagName, document));
}

SVGElement::~SVGElement()
{
    if (!hasRareSVGData())
        ASSERT(!SVGElementRareData::rareDataMap().contains(this));
    else {
        SVGElementRareData::SVGElementRareDataMap& rareDataMap = SVGElementRareData::rareDataMap();
        SVGElementRareData::SVGElementRareDataMap::iterator it = rareDataMap.find(this);
        ASSERT(it != rareDataMap.end());

        SVGElementRareData* rareData = it->second;
        rareData->destroyAnimatedSMILStyleProperties();
        if (SVGCursorElement* cursorElement = rareData->cursorElement())
            cursorElement->removeClient(this);
        if (CSSCursorImageValue* cursorImageValue = rareData->cursorImageValue())
            cursorImageValue->removeReferencedElement(this);

        delete rareData;
        rareDataMap.remove(it);
    }
    document()->accessSVGExtensions()->removeAllAnimationElementsFromTarget(this);
    document()->accessSVGExtensions()->removeAllElementReferencesForTarget(this);
}

bool SVGElement::willRecalcStyle(StyleChange change)
{
    if (!hasRareSVGData() || styleChangeType() == SyntheticStyleChange)
        return true;
    // If the style changes because of a regular property change (not induced by SMIL animations themselves)
    // reset the "computed style without SMIL style properties", so the base value change gets reflected.
    if (change > NoChange || needsStyleRecalc())
        rareSVGData()->setNeedsOverrideComputedStyleUpdate();
    return true;
}

SVGElementRareData* SVGElement::rareSVGData() const
{
    ASSERT(hasRareSVGData());
    return SVGElementRareData::rareDataFromMap(this);
}

SVGElementRareData* SVGElement::ensureRareSVGData()
{
    if (hasRareSVGData())
        return rareSVGData();

    ASSERT(!SVGElementRareData::rareDataMap().contains(this));
    SVGElementRareData* data = new SVGElementRareData;
    SVGElementRareData::rareDataMap().set(this, data);
    setHasRareSVGData();
    return data;
}

bool SVGElement::isOutermostSVGSVGElement() const
{
    if (!hasTagName(SVGNames::svgTag))
        return false;

    // If we're living in a shadow tree, we're a <svg> element that got created as replacement
    // for a <symbol> element or a cloned <svg> element in the referenced tree. In that case
    // we're always an inner <svg> element.
    if (isInShadowTree())
        return false;

    // Element may not be in the document, pretend we're outermost for viewport(), getCTM(), etc.
    if (!parentNode())
        return true;

    // We act like an outermost SVG element, if we're a direct child of a <foreignObject> element.
    if (parentNode()->hasTagName(SVGNames::foreignObjectTag))
        return true;

    // This is true whenever this is the outermost SVG, even if there are HTML elements outside it
    return !parentNode()->isSVGElement();
}

void SVGElement::reportAttributeParsingError(SVGParsingError error, Attribute* attribute)
{
    if (error == NoError)
        return;

    String errorString = "<" + tagName() + "> attribute " + attribute->name().toString() + "=\"" + attribute->value() + "\"";
    SVGDocumentExtensions* extensions = document()->accessSVGExtensions();

    if (error == NegativeValueForbiddenError) {
        extensions->reportError("Invalid negative value for " + errorString);
        return;
    }

    if (error == ParsingAttributeFailedError) {
        extensions->reportError("Invalid value for " + errorString);
        return;
    }

    ASSERT_NOT_REACHED();
}


bool SVGElement::isSupported(StringImpl* feature, StringImpl* version) const
{
    return DOMImplementation::hasFeature(feature, version);
}

String SVGElement::xmlbase() const
{
    return fastGetAttribute(XMLNames::baseAttr);
}

void SVGElement::setXmlbase(const String& value, ExceptionCode&)
{
    setAttribute(XMLNames::baseAttr, value);
}

void SVGElement::removedFrom(Node* rootParent)
{
    if (rootParent->inDocument()) {
        document()->accessSVGExtensions()->removeAllAnimationElementsFromTarget(this);
        document()->accessSVGExtensions()->removeAllElementReferencesForTarget(this);
    }

    StyledElement::removedFrom(rootParent);
}

SVGSVGElement* SVGElement::ownerSVGElement() const
{
    ContainerNode* n = parentOrHostNode();
    while (n) {
        if (n->hasTagName(SVGNames::svgTag))
            return static_cast<SVGSVGElement*>(n);

        n = n->parentOrHostNode();
    }

    return 0;
}

SVGElement* SVGElement::viewportElement() const
{
    // This function needs shadow tree support - as RenderSVGContainer uses this function
    // to determine the "overflow" property. <use> on <symbol> wouldn't work otherwhise.
    ContainerNode* n = parentOrHostNode();
    while (n) {
        if (n->hasTagName(SVGNames::svgTag) || n->hasTagName(SVGNames::imageTag) || n->hasTagName(SVGNames::symbolTag))
            return static_cast<SVGElement*>(n);

        n = n->parentOrHostNode();
    }

    return 0;
}

SVGDocumentExtensions* SVGElement::accessDocumentSVGExtensions()
{
    // This function is provided for use by SVGAnimatedProperty to avoid
    // global inclusion of Document.h in SVG code.
    return document() ? document()->accessSVGExtensions() : 0;
}
 
void SVGElement::mapInstanceToElement(SVGElementInstance* instance)
{
    ASSERT(instance);

    HashSet<SVGElementInstance*>& instances = ensureRareSVGData()->elementInstances();
    ASSERT(!instances.contains(instance));

    instances.add(instance);
}
 
void SVGElement::removeInstanceMapping(SVGElementInstance* instance)
{
    ASSERT(instance);
    ASSERT(hasRareSVGData());

    HashSet<SVGElementInstance*>& instances = rareSVGData()->elementInstances();
    ASSERT(instances.contains(instance));

    instances.remove(instance);
}

const HashSet<SVGElementInstance*>& SVGElement::instancesForElement() const
{
    if (!hasRareSVGData()) {
        DEFINE_STATIC_LOCAL(HashSet<SVGElementInstance*>, emptyInstances, ());
        return emptyInstances;
    }
    return rareSVGData()->elementInstances();
}

bool SVGElement::boundingBox(FloatRect& rect, SVGLocatable::StyleUpdateStrategy styleUpdateStrategy)
{
    if (isStyledLocatable()) {
        rect = static_cast<SVGStyledLocatableElement*>(this)->getBBox(styleUpdateStrategy);
        return true;
    }
    if (hasTagName(SVGNames::textTag)) {
        rect = static_cast<SVGTextElement*>(this)->getBBox(styleUpdateStrategy);
        return true;
    }
    return false;
}

void SVGElement::setCursorElement(SVGCursorElement* cursorElement)
{
    SVGElementRareData* rareData = ensureRareSVGData();
    if (SVGCursorElement* oldCursorElement = rareData->cursorElement()) {
        if (cursorElement == oldCursorElement)
            return;
        oldCursorElement->removeReferencedElement(this);
    }
    rareData->setCursorElement(cursorElement);
}

void SVGElement::cursorElementRemoved() 
{
    ASSERT(hasRareSVGData());
    rareSVGData()->setCursorElement(0);
}

void SVGElement::setCursorImageValue(CSSCursorImageValue* cursorImageValue)
{
    SVGElementRareData* rareData = ensureRareSVGData();
    if (CSSCursorImageValue* oldCursorImageValue = rareData->cursorImageValue()) {
        if (cursorImageValue == oldCursorImageValue)
            return;
        oldCursorImageValue->removeReferencedElement(this);
    }
    rareData->setCursorImageValue(cursorImageValue);
}

void SVGElement::cursorImageValueRemoved()
{
    ASSERT(hasRareSVGData());
    rareSVGData()->setCursorImageValue(0);
}

SVGElement* SVGElement::correspondingElement()
{
    ASSERT(!hasRareSVGData() || !rareSVGData()->correspondingElement() || shadowTreeRootNode());
    return hasRareSVGData() ? rareSVGData()->correspondingElement() : 0;
}

void SVGElement::setCorrespondingElement(SVGElement* correspondingElement)
{
    ensureRareSVGData()->setCorrespondingElement(correspondingElement);
}

void SVGElement::parseAttribute(Attribute* attr)
{
    // standard events
    if (attr->name() == onloadAttr)
        setAttributeEventListener(eventNames().loadEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == onclickAttr)
        setAttributeEventListener(eventNames().clickEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == onmousedownAttr)
        setAttributeEventListener(eventNames().mousedownEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == onmousemoveAttr)
        setAttributeEventListener(eventNames().mousemoveEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == onmouseoutAttr)
        setAttributeEventListener(eventNames().mouseoutEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == onmouseoverAttr)
        setAttributeEventListener(eventNames().mouseoverEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == onmouseupAttr)
        setAttributeEventListener(eventNames().mouseupEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == SVGNames::onfocusinAttr)
        setAttributeEventListener(eventNames().focusinEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == SVGNames::onfocusoutAttr)
        setAttributeEventListener(eventNames().focusoutEvent, createAttributeEventListener(this, attr));
    else if (attr->name() == SVGNames::onactivateAttr)
        setAttributeEventListener(eventNames().DOMActivateEvent, createAttributeEventListener(this, attr));
    else
        StyledElement::parseAttribute(attr);
}

void SVGElement::animatedPropertyTypeForAttribute(const QualifiedName& attributeName, Vector<AnimatedPropertyType>& propertyTypes)
{
    localAttributeToPropertyMap().animatedPropertyTypeForAttribute(attributeName, propertyTypes);
}

bool SVGElement::haveLoadedRequiredResources()
{
    Node* child = firstChild();
    while (child) {
        if (child->isSVGElement() && !static_cast<SVGElement*>(child)->haveLoadedRequiredResources())
            return false;
        child = child->nextSibling();
    }
    return true;
}

static inline void collectInstancesForSVGElement(SVGElement* element, HashSet<SVGElementInstance*>& instances)
{
    ASSERT(element);
    if (element->shadowTreeRootNode())
        return;

    if (!element->isStyled())
        return;

    SVGStyledElement* styledElement = static_cast<SVGStyledElement*>(element);
    ASSERT(!styledElement->instanceUpdatesBlocked());

    instances = styledElement->instancesForElement();
}

bool SVGElement::addEventListener(const AtomicString& eventType, PassRefPtr<EventListener> listener, bool useCapture)
{
    HashSet<SVGElementInstance*> instances;
    collectInstancesForSVGElement(this, instances);
    if (instances.isEmpty())
        return Node::addEventListener(eventType, listener, useCapture);

    RefPtr<EventListener> listenerForRegularTree = listener;
    RefPtr<EventListener> listenerForShadowTree = listenerForRegularTree;

    // Add event listener to regular DOM element
    if (!Node::addEventListener(eventType, listenerForRegularTree.release(), useCapture))
        return false;

    // Add event listener to all shadow tree DOM element instances
    const HashSet<SVGElementInstance*>::const_iterator end = instances.end();
    for (HashSet<SVGElementInstance*>::const_iterator it = instances.begin(); it != end; ++it) {
        ASSERT((*it)->shadowTreeElement());
        ASSERT((*it)->correspondingElement() == this);

        RefPtr<EventListener> listenerForCurrentShadowTreeElement = listenerForShadowTree;
        bool result = (*it)->shadowTreeElement()->Node::addEventListener(eventType, listenerForCurrentShadowTreeElement.release(), useCapture);
        ASSERT_UNUSED(result, result);
    }

    return true;
}

bool SVGElement::removeEventListener(const AtomicString& eventType, EventListener* listener, bool useCapture)
{
    HashSet<SVGElementInstance*> instances;
    collectInstancesForSVGElement(this, instances);
    if (instances.isEmpty())
        return Node::removeEventListener(eventType, listener, useCapture);

    // EventTarget::removeEventListener creates a PassRefPtr around the given EventListener
    // object when creating a temporary RegisteredEventListener object used to look up the
    // event listener in a cache. If we want to be able to call removeEventListener() multiple
    // times on different nodes, we have to delay its immediate destruction, which would happen
    // after the first call below.
    RefPtr<EventListener> protector(listener);

    // Remove event listener from regular DOM element
    if (!Node::removeEventListener(eventType, listener, useCapture))
        return false;

    // Remove event listener from all shadow tree DOM element instances
    const HashSet<SVGElementInstance*>::const_iterator end = instances.end();
    for (HashSet<SVGElementInstance*>::const_iterator it = instances.begin(); it != end; ++it) {
        ASSERT((*it)->correspondingElement() == this);

        SVGElement* shadowTreeElement = (*it)->shadowTreeElement();
        ASSERT(shadowTreeElement);

        if (shadowTreeElement->Node::removeEventListener(eventType, listener, useCapture))
            continue;

        // This case can only be hit for event listeners created from markup
        ASSERT(listener->wasCreatedFromMarkup());

        // If the event listener 'listener' has been created from markup and has been fired before
        // then JSLazyEventListener::parseCode() has been called and m_jsFunction of that listener
        // has been created (read: it's not 0 anymore). During shadow tree creation, the event
        // listener DOM attribute has been cloned, and another event listener has been setup in
        // the shadow tree. If that event listener has not been used yet, m_jsFunction is still 0,
        // and tryRemoveEventListener() above will fail. Work around that very seldom problem.
        EventTargetData* data = shadowTreeElement->eventTargetData();
        ASSERT(data);

        data->eventListenerMap.removeFirstEventListenerCreatedFromMarkup(eventType);
    }

    return true;
}

static bool hasLoadListener(Element* element)
{
    if (element->hasEventListeners(eventNames().loadEvent))
        return true;

    for (element = element->parentOrHostElement(); element; element = element->parentOrHostElement()) {
        const EventListenerVector& entry = element->getEventListeners(eventNames().loadEvent);
        for (size_t i = 0; i < entry.size(); ++i) {
            if (entry[i].useCapture)
                return true;
        }
    }

    return false;
}

void SVGElement::sendSVGLoadEventIfPossible(bool sendParentLoadEvents)
{
    RefPtr<SVGElement> currentTarget = this;
    while (currentTarget && currentTarget->haveLoadedRequiredResources()) {
        RefPtr<Element> parent;
        if (sendParentLoadEvents)
            parent = currentTarget->parentOrHostElement(); // save the next parent to dispatch too incase dispatching the event changes the tree
        if (hasLoadListener(currentTarget.get()))
            currentTarget->dispatchEvent(Event::create(eventNames().loadEvent, false, false));
        currentTarget = (parent && parent->isSVGElement()) ? static_pointer_cast<SVGElement>(parent) : RefPtr<SVGElement>();
        SVGElement* element = static_cast<SVGElement*>(currentTarget.get());
        if (!element || !element->isOutermostSVGSVGElement())
            continue;

        // Consider <svg onload="foo()"><image xlink:href="foo.png" externalResourcesRequired="true"/></svg>.
        // If foo.png is not yet loaded, the first SVGLoad event will go to the <svg> element, sent through
        // Document::implicitClose(). Then the SVGLoad event will fire for <image>, once its loaded.
        ASSERT(sendParentLoadEvents);

        // If the load event was not sent yet by Document::implicitClose(), but the <image> from the example
        // above, just appeared, don't send the SVGLoad event to the outermost <svg>, but wait for the document
        // to be "ready to render", first.
        if (!document()->loadEventFinished())
            break;
    }
}

void SVGElement::finishParsingChildren()
{
    StyledElement::finishParsingChildren();

    // The outermost SVGSVGElement SVGLoad event is fired through Document::dispatchWindowLoadEvent.
    if (isOutermostSVGSVGElement())
        return;

    // finishParsingChildren() is called when the close tag is reached for an element (e.g. </svg>)
    // we send SVGLoad events here if we can, otherwise they'll be sent when any required loads finish
    sendSVGLoadEventIfPossible();
}

bool SVGElement::childShouldCreateRenderer(const NodeRenderingContext& childContext) const
{
    if (childContext.node()->isSVGElement())
        return static_cast<SVGElement*>(childContext.node())->isValid();
    return false;
}

void SVGElement::attributeChanged(Attribute* attr)
{
    ASSERT(attr);
    StyledElement::attributeChanged(attr);

    // When an animated SVG property changes through SVG DOM, svgAttributeChanged() is called, not attributeChanged().
    // Next time someone tries to access the XML attributes, the synchronization code starts. During that synchronization
    // SVGAnimatedPropertySynchronizer may call ElementAttributeData::removeAttribute(), which in turn calls attributeChanged().
    // At this point we're not allowed to call svgAttributeChanged() again - it may lead to extra work being done, or crashes
    // see bug https://bugs.webkit.org/show_bug.cgi?id=40994.
    if (isSynchronizingSVGAttributes())
        return;

    if (isIdAttributeName(attr->name())) {
        document()->accessSVGExtensions()->removeAllAnimationElementsFromTarget(this);
        document()->accessSVGExtensions()->removeAllElementReferencesForTarget(this);
    }

    // Changes to the style attribute are processed lazily (see Element::getAttribute() and related methods),
    // so we don't want changes to the style attribute to result in extra work here.
    if (attr->name() != HTMLNames::styleAttr)
        svgAttributeChanged(attr->name());
}

void SVGElement::updateAnimatedSVGAttribute(const QualifiedName& name) const
{
    if (isSynchronizingSVGAttributes() || areSVGAttributesValid())
        return;

    setIsSynchronizingSVGAttributes();

    SVGElement* nonConstThis = const_cast<SVGElement*>(this);
    if (name == anyQName()) {
        nonConstThis->localAttributeToPropertyMap().synchronizeProperties(nonConstThis);
        setAreSVGAttributesValid();
    } else
        nonConstThis->localAttributeToPropertyMap().synchronizeProperty(nonConstThis, name);

    clearIsSynchronizingSVGAttributes();
}

SVGAttributeToPropertyMap& SVGElement::localAttributeToPropertyMap()
{
    ASSERT_NOT_REACHED();

    DEFINE_STATIC_LOCAL(SVGAttributeToPropertyMap, dummyMap, ());
    return dummyMap;
}

void SVGElement::synchronizeRequiredFeatures(void* contextElement)
{
    ASSERT(contextElement);
    static_cast<SVGElement*>(contextElement)->synchronizeRequiredFeatures();
}

void SVGElement::synchronizeRequiredExtensions(void* contextElement)
{
    ASSERT(contextElement);
    static_cast<SVGElement*>(contextElement)->synchronizeRequiredExtensions();
}

void SVGElement::synchronizeSystemLanguage(void* contextElement)
{
    ASSERT(contextElement);
    static_cast<SVGElement*>(contextElement)->synchronizeSystemLanguage();
}

PassRefPtr<RenderStyle> SVGElement::customStyleForRenderer()
{
    if (!correspondingElement())
        return document()->styleResolver()->styleForElement(this);

    RenderStyle* style = 0;
    if (Element* parent = parentOrHostElement()) {
        if (RenderObject* renderer = parent->renderer())
            style = renderer->style();
    }

    return document()->styleResolver()->styleForElement(correspondingElement(), style, DisallowStyleSharing);
}

StylePropertySet* SVGElement::animatedSMILStyleProperties() const
{
    if (hasRareSVGData())
        return rareSVGData()->animatedSMILStyleProperties();
    return 0;
}

StylePropertySet* SVGElement::ensureAnimatedSMILStyleProperties()
{
    return ensureRareSVGData()->ensureAnimatedSMILStyleProperties();
}

void SVGElement::setUseOverrideComputedStyle(bool value)
{
    if (hasRareSVGData())
        rareSVGData()->setUseOverrideComputedStyle(value);
}

RenderStyle* SVGElement::computedStyle(PseudoId pseudoElementSpecifier)
{
    if (!hasRareSVGData() || !rareSVGData()->useOverrideComputedStyle())
        return Element::computedStyle(pseudoElementSpecifier);

    RenderStyle* parentStyle = 0;
    if (Element* parent = parentOrHostElement()) {
        if (RenderObject* renderer = parent->renderer())
            parentStyle = renderer->style();
    }

    return rareSVGData()->overrideComputedStyle(this, parentStyle);
}

#ifndef NDEBUG
bool SVGElement::isAnimatableAttribute(const QualifiedName& name)
{
    DEFINE_STATIC_LOCAL(HashSet<QualifiedName>, animatableAttributes, ());

    if (animatableAttributes.isEmpty()) {
        animatableAttributes.add(classAttr);
        animatableAttributes.add(XLinkNames::hrefAttr);
        animatableAttributes.add(SVGNames::amplitudeAttr);
        animatableAttributes.add(SVGNames::azimuthAttr);
        animatableAttributes.add(SVGNames::baseFrequencyAttr);
        animatableAttributes.add(SVGNames::biasAttr);
        animatableAttributes.add(SVGNames::clipPathUnitsAttr);
        animatableAttributes.add(SVGNames::cxAttr);
        animatableAttributes.add(SVGNames::cyAttr);
        animatableAttributes.add(SVGNames::diffuseConstantAttr);
        animatableAttributes.add(SVGNames::divisorAttr);
        animatableAttributes.add(SVGNames::dxAttr);
        animatableAttributes.add(SVGNames::dyAttr);
        animatableAttributes.add(SVGNames::edgeModeAttr);
        animatableAttributes.add(SVGNames::elevationAttr);
        animatableAttributes.add(SVGNames::exponentAttr);
        animatableAttributes.add(SVGNames::externalResourcesRequiredAttr);
        animatableAttributes.add(SVGNames::filterResAttr);
        animatableAttributes.add(SVGNames::filterUnitsAttr);
        animatableAttributes.add(SVGNames::fxAttr);
        animatableAttributes.add(SVGNames::fyAttr);
        animatableAttributes.add(SVGNames::gradientTransformAttr);
        animatableAttributes.add(SVGNames::gradientUnitsAttr);
        animatableAttributes.add(SVGNames::heightAttr);
        animatableAttributes.add(SVGNames::in2Attr);
        animatableAttributes.add(SVGNames::inAttr);
        animatableAttributes.add(SVGNames::interceptAttr);
        animatableAttributes.add(SVGNames::k1Attr);
        animatableAttributes.add(SVGNames::k2Attr);
        animatableAttributes.add(SVGNames::k3Attr);
        animatableAttributes.add(SVGNames::k4Attr);
        animatableAttributes.add(SVGNames::kernelMatrixAttr);
        animatableAttributes.add(SVGNames::kernelUnitLengthAttr);
        animatableAttributes.add(SVGNames::lengthAdjustAttr);
        animatableAttributes.add(SVGNames::limitingConeAngleAttr);
        animatableAttributes.add(SVGNames::markerHeightAttr);
        animatableAttributes.add(SVGNames::markerUnitsAttr);
        animatableAttributes.add(SVGNames::markerWidthAttr);
        animatableAttributes.add(SVGNames::maskContentUnitsAttr);
        animatableAttributes.add(SVGNames::maskUnitsAttr);
        animatableAttributes.add(SVGNames::methodAttr);
        animatableAttributes.add(SVGNames::modeAttr);
        animatableAttributes.add(SVGNames::numOctavesAttr);
        animatableAttributes.add(SVGNames::offsetAttr);
        animatableAttributes.add(SVGNames::operatorAttr);
        animatableAttributes.add(SVGNames::orderAttr);
        animatableAttributes.add(SVGNames::orientAttr);
        animatableAttributes.add(SVGNames::pathLengthAttr);
        animatableAttributes.add(SVGNames::patternContentUnitsAttr);
        animatableAttributes.add(SVGNames::patternTransformAttr);
        animatableAttributes.add(SVGNames::patternUnitsAttr);
        animatableAttributes.add(SVGNames::pointsAtXAttr);
        animatableAttributes.add(SVGNames::pointsAtYAttr);
        animatableAttributes.add(SVGNames::pointsAtZAttr);
        animatableAttributes.add(SVGNames::preserveAlphaAttr);
        animatableAttributes.add(SVGNames::preserveAspectRatioAttr);
        animatableAttributes.add(SVGNames::primitiveUnitsAttr);
        animatableAttributes.add(SVGNames::radiusAttr);
        animatableAttributes.add(SVGNames::rAttr);
        animatableAttributes.add(SVGNames::refXAttr);
        animatableAttributes.add(SVGNames::refYAttr);
        animatableAttributes.add(SVGNames::resultAttr);
        animatableAttributes.add(SVGNames::rotateAttr);
        animatableAttributes.add(SVGNames::rxAttr);
        animatableAttributes.add(SVGNames::ryAttr);
        animatableAttributes.add(SVGNames::scaleAttr);
        animatableAttributes.add(SVGNames::seedAttr);
        animatableAttributes.add(SVGNames::slopeAttr);
        animatableAttributes.add(SVGNames::spacingAttr);
        animatableAttributes.add(SVGNames::specularConstantAttr);
        animatableAttributes.add(SVGNames::specularExponentAttr);
        animatableAttributes.add(SVGNames::spreadMethodAttr);
        animatableAttributes.add(SVGNames::startOffsetAttr);
        animatableAttributes.add(SVGNames::stdDeviationAttr);
        animatableAttributes.add(SVGNames::stitchTilesAttr);
        animatableAttributes.add(SVGNames::surfaceScaleAttr);
        animatableAttributes.add(SVGNames::tableValuesAttr);
        animatableAttributes.add(SVGNames::targetAttr);
        animatableAttributes.add(SVGNames::targetXAttr);
        animatableAttributes.add(SVGNames::targetYAttr);
        animatableAttributes.add(SVGNames::transformAttr);
        animatableAttributes.add(SVGNames::typeAttr);
        animatableAttributes.add(SVGNames::valuesAttr);
        animatableAttributes.add(SVGNames::viewBoxAttr);
        animatableAttributes.add(SVGNames::widthAttr);
        animatableAttributes.add(SVGNames::x1Attr);
        animatableAttributes.add(SVGNames::x2Attr);
        animatableAttributes.add(SVGNames::xAttr);
        animatableAttributes.add(SVGNames::xChannelSelectorAttr);
        animatableAttributes.add(SVGNames::y1Attr);
        animatableAttributes.add(SVGNames::y2Attr);
        animatableAttributes.add(SVGNames::yAttr);
        animatableAttributes.add(SVGNames::yChannelSelectorAttr);
        animatableAttributes.add(SVGNames::zAttr);
    }
    return animatableAttributes.contains(name);
}
#endif

}

#endif // ENABLE(SVG)
