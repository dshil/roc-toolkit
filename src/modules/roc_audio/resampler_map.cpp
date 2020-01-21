/*
 * Copyright (c) 2019 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/resampler_map.h"
#include "roc_audio/resampler_builtin.h"
#include "roc_audio/resampler_config.h"

namespace roc {
namespace audio {

IResampler* ResamplerMap::new_resampler(ResamplerBackend resampler_backend,
                                        core::IAllocator& allocator,
                                        const ResamplerConfig& config,
                                        packet::channel_mask_t channels,
                                        size_t frame_size) {
    if (resampler_backend != ResamplerBackend_Builtin) {
        roc_panic(
            "No valid resampler backend selected.. selected resampler backend is : %d",
            resampler_backend);
    }

    core::ScopedPtr<IResampler> resampler;
    switch (resampler_backend) {
    case ResamplerBackend_Builtin:
        resampler.reset(new (allocator)
                            BuiltinResampler(allocator, config, channels, frame_size),
                        allocator);
        break;
    }

    if (!resampler || !resampler->valid()) {
        return NULL;
    }

    return resampler.release();
}

} // namespace audio
} // namespace roc