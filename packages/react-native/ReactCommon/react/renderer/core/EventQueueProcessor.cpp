/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cxxreact/JSExecutor.h>
#include <logger/react_native_log.h>
#include "EventEmitter.h"
#include "EventLogger.h"
#include "EventQueue.h"
#include "ShadowNodeFamily.h"

namespace facebook::react {

EventQueueProcessor::EventQueueProcessor(
    EventPipe eventPipe,
    StatePipe statePipe)
    : eventPipe_(std::move(eventPipe)), statePipe_(std::move(statePipe)) {}

void EventQueueProcessor::flushEvents(
    jsi::Runtime &runtime,
    std::vector<RawEvent> &&events) const {
  {
    std::scoped_lock lock(EventEmitter::DispatchMutex());

    for (const auto &event : events) {
      if (event.eventTarget) {
        event.eventTarget->retain(runtime);
      }
    }
  }

  for (auto const &event : events) {
    if (event.category == RawEvent::Category::ContinuousEnd) {
      hasContinuousEventStarted_ = false;
    }

    auto reactPriority = hasContinuousEventStarted_
        ? ReactEventPriority::Default
        : ReactEventPriority::Discrete;

    if (event.category == RawEvent::Category::Continuous) {
      reactPriority = ReactEventPriority::Default;
    }

    if (event.category == RawEvent::Category::Discrete) {
      reactPriority = ReactEventPriority::Discrete;
    }

    auto eventLogger = getEventLogger();
    if (eventLogger != nullptr) {
      eventLogger->onEventDispatch(event.loggingTag);
    }

    if (event.eventPayload == nullptr) {
      react_native_log_error(
          "EventQueueProcessor: Unexpected null event payload");
      continue;
    }

    eventPipe_(
        runtime,
        event.eventTarget.get(),
        event.type,
        reactPriority,
        *event.eventPayload);

    if (eventLogger != nullptr) {
      eventLogger->onEventEnd(event.loggingTag);
    }

    if (event.category == RawEvent::Category::ContinuousStart) {
      hasContinuousEventStarted_ = true;
    }
  }

  // No need to lock `EventEmitter::DispatchMutex()` here.
  // The mutex protects from a situation when the `instanceHandle` can be
  // deallocated during accessing, but that's impossible at this point because
  // we have a strong pointer to it.
  for (const auto &event : events) {
    if (event.eventTarget) {
      event.eventTarget->release(runtime);
    }
  }
}

void EventQueueProcessor::flushStateUpdates(
    std::vector<StateUpdate> &&states) const {
  for (const auto &stateUpdate : states) {
    statePipe_(stateUpdate);
  }
}

} // namespace facebook::react
