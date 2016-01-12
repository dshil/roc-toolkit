/*
 * Copyright (c) 2015 Mikhail Baranov
 * Copyright (c) 2015 Victor Gaydov
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_core/panic.h"
#include "roc_core/log.h"
#include "roc_core/math.h"

#include "roc_audio/watchdog.h"

#define SEQ_IS_BEFORE(a, b) ROC_IS_BEFORE(packet::signed_seqnum_t, a, b)
#define SEQ_SUBTRACT(a, b) ROC_SUBTRACT(packet::signed_seqnum_t, a, b)
#define TS_SUBTRACT(a, b) ROC_SUBTRACT(packet::signed_timestamp_t, a, b)

namespace roc {
namespace audio {

Watchdog::Watchdog(packet::IPacketReader& reader, size_t timeout)
    : reader_(reader)
    , timeout_(timeout)
    , countdown_(timeout)
    , has_packets_(false)
    , alive_(true) {
}

bool Watchdog::update() {
    if (!alive_) {
        return false;
    }

    if (has_packets_) {
        countdown_ = timeout_;
    } else {
        if (countdown_ > 0) {
            countdown_--;
        }
        if (countdown_ == 0) {
            roc_log(LOG_DEBUG, "watchdog: timeout reached (%u ticks without packets)",
                    (unsigned)timeout_);
            return (alive_ = false);
        }
    }

    has_packets_ = false;
    return true;
}

packet::IPacketConstPtr Watchdog::read() {
    if (!alive_) {
        return NULL;
    }

    packet::IPacketConstPtr packet = reader_.read();
    if (!packet) {
        return NULL;
    }

    if (detect_jump_(packet)) {
        alive_ = false;
        return NULL;
    }

    has_packets_ = true;

    return packet;
}

bool Watchdog::detect_jump_(const packet::IPacketConstPtr& next) {
    if (prev_) {
        packet::signed_seqnum_t sn_dist = SEQ_SUBTRACT(prev_->seqnum(), next->seqnum());

        if (ROC_ABS(sn_dist) > ROC_CONFIG_MAX_SN_JUMP) {
            roc_log(LOG_DEBUG, "watchdog: too long seqnum jump:"
                               " prev=%lu next=%lu dist=%ld",
                    (unsigned long)prev_->seqnum(), (unsigned long)next->seqnum(),
                    (long)sn_dist);
            return true;
        }
    }

    if (prev_) {
        packet::signed_timestamp_t ts_dist =
            TS_SUBTRACT(prev_->timestamp(), next->timestamp());

        if (ROC_ABS(ts_dist) > ROC_CONFIG_MAX_TS_JUMP) {
            roc_log(LOG_DEBUG, "watchdog: too long timestamp jump:"
                               " prev=%lu next=%lu dist=%ld",
                    (unsigned long)prev_->timestamp(), (unsigned long)next->timestamp(),
                    (long)ts_dist);
            return true;
        }
    }

    if (!prev_ || SEQ_IS_BEFORE(prev_->seqnum(), next->seqnum())) {
        prev_ = next;
    }

    return false;
}

} // namespace audio
} // namespace roc
