/*
 * Copyright (c) 2020 Roc Streaming authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_pipeline/sender_session.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"
#include "roc_fec/codec_map.h"

namespace roc {
namespace pipeline {

SenderSession::SenderSession(const SenderConfig& config,
                             const rtp::FormatMap& format_map,
                             packet::PacketFactory& packet_factory,
                             core::BufferFactory<uint8_t>& byte_buffer_factory,
                             core::BufferFactory<audio::sample_t>& sample_buffer_factory,
                             core::IAllocator& allocator)
    : allocator_(allocator)
    , config_(config)
    , format_map_(format_map)
    , packet_factory_(packet_factory)
    , byte_buffer_factory_(byte_buffer_factory)
    , sample_buffer_factory_(sample_buffer_factory)
    , audio_writer_(NULL)
    , num_sources_(0) {
}

bool SenderSession::create_transport_pipeline(SenderEndpoint* source_endpoint,
                                              SenderEndpoint* repair_endpoint) {
    roc_panic_if(audio_writer_);
    roc_panic_if(!source_endpoint);

    if (source_endpoint) {
        num_sources_++;
    }

    if (repair_endpoint) {
        num_sources_++;
    }

    const rtp::Format* format = format_map_.format(config_.payload_type);
    if (!format) {
        return false;
    }

    router_.reset(new (router_) packet::Router(allocator_));
    if (!router_) {
        return false;
    }
    packet::IWriter* pwriter = router_.get();

    if (!router_->add_route(source_endpoint->writer(), packet::Packet::FlagAudio)) {
        return false;
    }

    if (repair_endpoint) {
        if (!router_->add_route(repair_endpoint->writer(), packet::Packet::FlagRepair)) {
            return false;
        }

        if (config_.interleaving) {
            interleaver_.reset(new (interleaver_) packet::Interleaver(
                *pwriter, allocator_,
                config_.fec_writer.n_source_packets
                    + config_.fec_writer.n_repair_packets));
            if (!interleaver_ || !interleaver_->valid()) {
                return false;
            }
            pwriter = interleaver_.get();
        }

        fec_encoder_.reset(fec::CodecMap::instance().new_encoder(
                               config_.fec_encoder, byte_buffer_factory_, allocator_),
                           allocator_);
        if (!fec_encoder_) {
            return false;
        }

        fec_writer_.reset(new (fec_writer_) fec::Writer(
            config_.fec_writer, config_.fec_encoder.scheme, *fec_encoder_, *pwriter,
            source_endpoint->composer(), repair_endpoint->composer(), packet_factory_,
            byte_buffer_factory_, allocator_));
        if (!fec_writer_ || !fec_writer_->valid()) {
            return false;
        }
        pwriter = fec_writer_.get();
    }

    payload_encoder_.reset(format->new_encoder(allocator_), allocator_);
    if (!payload_encoder_) {
        return false;
    }

    packetizer_.reset(new (packetizer_) audio::Packetizer(
        *pwriter, source_endpoint->composer(), *payload_encoder_, packet_factory_,
        byte_buffer_factory_, config_.packet_length, format->sample_spec,
        config_.payload_type));
    if (!packetizer_ || !packetizer_->valid()) {
        return false;
    }

    audio::IFrameWriter* awriter = packetizer_.get();

    if (format->sample_spec.channel_mask() != config_.input_sample_spec.channel_mask()) {
        channel_mapper_writer_.reset(
            new (channel_mapper_writer_) audio::ChannelMapperWriter(
                *awriter, sample_buffer_factory_, config_.internal_frame_length,
                audio::SampleSpec(format->sample_spec.sample_rate(),
                                  config_.input_sample_spec.channel_mask()),
                format->sample_spec));
        if (!channel_mapper_writer_ || !channel_mapper_writer_->valid()) {
            return false;
        }
        awriter = channel_mapper_writer_.get();
    }

    if (config_.resampling
        && format->sample_spec.sample_rate() != config_.input_sample_spec.sample_rate()) {
        if (config_.poisoning) {
            resampler_poisoner_.reset(new (resampler_poisoner_)
                                          audio::PoisonWriter(*awriter));
            if (!resampler_poisoner_) {
                return false;
            }
            awriter = resampler_poisoner_.get();
        }

        resampler_.reset(audio::ResamplerMap::instance().new_resampler(
                             config_.resampler_backend, allocator_,
                             sample_buffer_factory_, config_.resampler_profile,
                             config_.internal_frame_length, config_.input_sample_spec),
                         allocator_);

        if (!resampler_) {
            return false;
        }

        resampler_writer_.reset(new (resampler_writer_) audio::ResamplerWriter(
            *awriter, *resampler_, sample_buffer_factory_, config_.internal_frame_length,
            config_.input_sample_spec,
            audio::SampleSpec(format->sample_spec.sample_rate(),
                              config_.input_sample_spec.channel_mask())));

        if (!resampler_writer_ || !resampler_writer_->valid()) {
            return false;
        }
        awriter = resampler_writer_.get();
    }

    audio_writer_ = awriter;

    return true;
}

bool SenderSession::create_control_pipeline(SenderEndpoint* control_endpoint) {
    roc_panic_if(rtcp_session_);
    roc_panic_if(!control_endpoint);

    rtcp_composer_.reset(new (rtcp_composer_) rtcp::Composer());
    if (!rtcp_composer_) {
        return false;
    }

    rtcp_session_.reset(new (rtcp_session_) rtcp::Session(
        NULL, this, &control_endpoint->writer(), *rtcp_composer_, packet_factory_,
        byte_buffer_factory_));
    if (!rtcp_session_ || !rtcp_session_->valid()) {
        return false;
    }

    return true;
}

audio::IFrameWriter* SenderSession::writer() const {
    return audio_writer_;
}

core::nanoseconds_t SenderSession::get_update_deadline() const {
    if (rtcp_session_) {
        return rtcp_session_->generation_deadline();
    }

    return 0;
}

void SenderSession::update() {
    if (rtcp_session_) {
        rtcp_session_->generate_packets();
    }
}

size_t SenderSession::on_get_num_sources() {
    return num_sources_;
}

packet::source_t SenderSession::on_get_sending_source(size_t source_index) {
    switch (source_index) {
    case 0:
        // TODO
        return 123;

    case 1:
        // TODO
        return 456;
    }

    roc_panic("sender slot: source index out of bounds: source_index=%lu",
              (unsigned long)source_index);
}

rtcp::SendingMetrics
SenderSession::on_get_sending_metrics(packet::ntp_timestamp_t report_time) {
    // TODO

    rtcp::SendingMetrics metrics;
    metrics.origin_ntp = report_time;

    return metrics;
}

void SenderSession::on_add_reception_metrics(const rtcp::ReceptionMetrics& metrics) {
    // TODO
    (void)metrics;
}

void SenderSession::on_add_link_metrics(const rtcp::LinkMetrics& metrics) {
    // TODO
    (void)metrics;
}

} // namespace pipeline
} // namespace roc
