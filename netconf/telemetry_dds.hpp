// DDS telemetry publisher (Eclipse Cyclone DDS). Publishes the same
// serialized Protocol Buffers sample as the ZeroMQ path, on DDS topic
// "onsim_telemetry" -- protobuf payloads over DDS pub/sub.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "dds/dds.h"
#include "onsim_dds.h"

namespace onsim {

class DdsTelemetryPublisher {
public:
    DdsTelemetryPublisher() {
        participant_ = dds_create_participant(DDS_DOMAIN_DEFAULT, nullptr, nullptr);
        if (participant_ < 0) throw std::runtime_error("dds participant");
        topic_ = dds_create_topic(participant_, &onsim_TelemetryBlob_desc,
                                  "onsim_telemetry", nullptr, nullptr);
        if (topic_ < 0) throw std::runtime_error("dds topic");
        writer_ = dds_create_writer(participant_, topic_, nullptr, nullptr);
        if (writer_ < 0) throw std::runtime_error("dds writer");
    }

    ~DdsTelemetryPublisher() { dds_delete(participant_); }

    DdsTelemetryPublisher(const DdsTelemetryPublisher&) = delete;
    DdsTelemetryPublisher& operator=(const DdsTelemetryPublisher&) = delete;

    void publish(uint64_t tick, const std::string& protobufPayload) {
        onsim_TelemetryBlob blob;
        blob.tick = tick;
        blob.payload._length = static_cast<uint32_t>(protobufPayload.size());
        blob.payload._maximum = blob.payload._length;
        blob.payload._buffer = reinterpret_cast<uint8_t*>(
            const_cast<char*>(protobufPayload.data()));
        blob.payload._release = false;  // we own the buffer, not DDS
        dds_write(writer_, &blob);
    }

private:
    dds_entity_t participant_ = 0;
    dds_entity_t topic_ = 0;
    dds_entity_t writer_ = 0;
};

}  // namespace onsim
