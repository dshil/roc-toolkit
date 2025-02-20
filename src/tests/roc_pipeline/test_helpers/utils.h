/*
 * Copyright (c) 2015 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef ROC_PIPELINE_TEST_HELPERS_UTILS_H_
#define ROC_PIPELINE_TEST_HELPERS_UTILS_H_

#include <CppUTest/TestHarness.h>

#include "roc_address/socket_addr.h"
#include "roc_audio/sample.h"
#include "roc_core/stddefs.h"

namespace roc {
namespace pipeline {
namespace test {

namespace {

const double Epsilon = 0.00001;

inline audio::sample_t nth_sample(uint8_t n) {
    return audio::sample_t(n) / 1024;
}

inline address::SocketAddr new_address(int port) {
    address::SocketAddr addr;
    CHECK(addr.set_host_port(address::Family_IPv4, "127.0.0.1", port));
    return addr;
}

} // namespace

} // namespace test
} // namespace pipeline
} // namespace roc

#endif // ROC_PIPELINE_TEST_HELPERS_UTILS_H_
