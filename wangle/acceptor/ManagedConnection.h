/*
 *  Copyright (c) 2016, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#pragma once

#include <folly/IntrusiveList.h>
#include <ostream>
#include <folly/io/async/HHWheelTimer.h>
#include <folly/io/async/DelayedDestruction.h>

namespace wangle {

class ConnectionManager;

/**
 * Interface describing a connection that can be managed by a
 * container such as an Acceptor.
 */
class ManagedConnection:
    public folly::HHWheelTimer::Callback,
    public folly::DelayedDestruction {
 public:

  ManagedConnection();

  class Callback {
  public:
    virtual ~Callback() = default;

    /* Invoked when this connection becomes busy */
    virtual void onActivated(ManagedConnection& conn) = 0;

    /* Invoked when a connection becomes idle */
    virtual void onDeactivated(ManagedConnection& conn) = 0;
  };

  // HHWheelTimer::Callback API (left for subclasses to implement).
  virtual void timeoutExpired() noexcept = 0;

  /**
   * Print a human-readable description of the connection.
   * @param os Destination stream.
   */
  virtual void describe(std::ostream& os) const = 0;

  /**
   * Check whether the connection has any requests outstanding.
   */
  virtual bool isBusy() const = 0;

  /**
   * Get the idle time of the connection. If it returning 0, that means the idle
   * connections will never be dropped during pre load shedding stage.
   */
  virtual std::chrono::milliseconds getIdleTime() const {
    return std::chrono::milliseconds(0);
  }

  /**
   * Notify the connection that a shutdown is pending. This method will be
   * called at the beginning of graceful shutdown.
   */
  virtual void notifyPendingShutdown() = 0;

  void fireNotifyPendingShutdown() {
    if (state_ == DrainState::NONE) {
      state_ = DrainState::SENT_NOTIFY_PENDING_SHUTDOWN;
      notifyPendingShutdown();
    }
  }

  /**
   * Instruct the connection that it should shutdown as soon as it is
   * safe. This is called after notifyPendingShutdown().
   */
  virtual void closeWhenIdle() = 0;

  void fireCloseWhenIdle(bool force_to_close = false) {
    if (force_to_close || state_ == DrainState::SENT_NOTIFY_PENDING_SHUTDOWN) {
      state_ = DrainState::SENT_CLOSE_WHEN_IDLE;
      closeWhenIdle();
    }
  }

  /**
   * Forcibly drop a connection.
   *
   * If a request is in progress, this should cause the connection to be
   * closed with a reset.
   */
  virtual void dropConnection() = 0;

  /**
   * Dump the state of the connection to the log
   */
  virtual void dumpConnectionState(uint8_t loglevel) = 0;

  /**
   * If the connection has a connection manager, reset the timeout countdown to
   * connection manager's default timeout.
   * @note If the connection manager doesn't have the connection scheduled
   *       for a timeout already, this method will schedule one.  If the
   *       connection manager does have the connection connection scheduled
   *       for a timeout, this method will push back the timeout to N msec
   *       from now, where N is the connection manager's timer interval.
   */
  virtual void resetTimeout();

  /**
   * If the connection has a connection manager, reset the timeout countdown to
   * user specified timeout.
   */
  void resetTimeoutTo(std::chrono::milliseconds);

  // Schedule an arbitrary timeout on the HHWheelTimer
  virtual void scheduleTimeout(
    folly::HHWheelTimer::Callback* callback,
    std::chrono::milliseconds timeout);

  ConnectionManager* getConnectionManager() {
    return connectionManager_;
  }

 protected:
  virtual ~ManagedConnection();

 private:
  enum class DrainState {
    NONE,
    SENT_NOTIFY_PENDING_SHUTDOWN,
    SENT_CLOSE_WHEN_IDLE,
  };

  DrainState state_{DrainState::NONE};

  friend class ConnectionManager;

  void setConnectionManager(ConnectionManager* mgr) {
    connectionManager_ = mgr;
  }

  ConnectionManager* connectionManager_;

  folly::SafeIntrusiveListHook listHook_;

};

std::ostream& operator<<(std::ostream& os, const ManagedConnection& conn);

} // wangle
