/*
 * Copyright (c) 2025, Tim Ledbetter <tim.ledbetter@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/NumericLimits.h>
#include <LibWeb/Bindings/ConstantSourceNodePrototype.h>
#include <LibWeb/Bindings/Intrinsics.h>
#include <LibWeb/WebAudio/BaseAudioContext.h>
#include <LibWeb/WebAudio/ConstantSourceNode.h>
#include <LibWeb/WebAudio/AudioParamRenderData.h>
#include <LibWeb/WebAudio/ControlMessage.h>
#include <math.h>

namespace Web::WebAudio {

GC_DEFINE_ALLOCATOR(ConstantSourceNode);

ConstantSourceNode::ConstantSourceNode(JS::Realm& realm, GC::Ref<BaseAudioContext> context, ConstantSourceOptions const& options)
    : AudioScheduledSourceNode(realm, context)
    , m_offset(AudioParam::create(realm, context, options.offset, NumericLimits<float>::lowest(), NumericLimits<float>::max(), Bindings::AutomationRate::ARate))
{
}

ConstantSourceNode::~ConstantSourceNode() = default;

WebIDL::ExceptionOr<void> ConstantSourceNode::start(double when)
{
    if (source_started())
        return WebIDL::InvalidStateError::create(realm(), "ConstantSourceNode source has already started"_utf16);
    if (!isfinite(when) || when < 0)
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "when must be finite and non-negative"sv };
    if (!isfinite(m_offset->value()))
        return WebIDL::SimpleException { WebIDL::SimpleExceptionType::RangeError, "Constant source offset must be finite"sv };
    auto automation = TRY(m_offset->create_render_data());
    set_source_started(true);
    context()->queue_control_message(StartConstantSource {
        .node_id = node_id(),
        .when = when,
        .offset = m_offset->value(),
        .offset_automation = move(automation),
        .offset_param_id = m_offset->param_id(),
    });
    return {};
}

WebIDL::ExceptionOr<GC::Ref<ConstantSourceNode>> ConstantSourceNode::create(JS::Realm& realm, GC::Ref<BaseAudioContext> context, ConstantSourceOptions const& options)
{
    return construct_impl(realm, context, options);
}

WebIDL::ExceptionOr<GC::Ref<ConstantSourceNode>> ConstantSourceNode::construct_impl(JS::Realm& realm, GC::Ref<BaseAudioContext> context, ConstantSourceOptions const& options)
{
    return realm.create<ConstantSourceNode>(realm, context, options);
}

void ConstantSourceNode::initialize(JS::Realm& realm)
{
    WEB_SET_PROTOTYPE_FOR_INTERFACE(ConstantSourceNode);
    Base::initialize(realm);
}

void ConstantSourceNode::visit_edges(Cell::Visitor& visitor)
{
    Base::visit_edges(visitor);
    visitor.visit(m_offset);
}

}
