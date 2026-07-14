// The NE-internal DDS message bus (Eclipse Cyclone DDS):
//  - DdsControlClient  (management plane): request/reply provisioning calls
//  - DdsControlServer  (device daemon): executes commands against the HAL
//  - DdsTelemetryCache (management plane): latest telemetry sample, used to
//    serve NETCONF operational-state reads without touching the device
//  - DdsTelemetryPublisher (device daemon): protobuf payloads on the wire
#pragma once

#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

#include "dds/dds.h"
#include "onsim_dds.h"
#include "onsim_telemetry.pb.h"

namespace onsim {

inline dds_entity_t makeParticipant() {
    dds_entity_t p = dds_create_participant(DDS_DOMAIN_DEFAULT, nullptr, nullptr);
    if (p < 0) throw std::runtime_error("dds participant");
    return p;
}

inline dds_qos_t* reliableQos() {
    dds_qos_t* qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    return qos;
}

struct ControlResult {
    int64_t status = 0;
    std::string error;
    bool ok() const { return status == 0; }
};

// Management-plane side: send a command, wait for the correlated reply.
class DdsControlClient {
public:
    DdsControlClient() : participant_(makeParticipant()) {
        dds_entity_t cmdTopic = dds_create_topic(
            participant_, &onsim_ControlCommand_desc, "onsim_control_cmd",
            nullptr, nullptr);
        dds_entity_t repTopic = dds_create_topic(
            participant_, &onsim_ControlReply_desc, "onsim_control_reply",
            nullptr, nullptr);
        dds_qos_t* qos = reliableQos();
        writer_ = dds_create_writer(participant_, cmdTopic, qos, nullptr);
        reader_ = dds_create_reader(participant_, repTopic, qos, nullptr);
        dds_delete_qos(qos);
        if (writer_ < 0 || reader_ < 0) throw std::runtime_error("dds control client");
    }
    ~DdsControlClient() { dds_delete(participant_); }
    DdsControlClient(const DdsControlClient&) = delete;
    DdsControlClient& operator=(const DdsControlClient&) = delete;

    // Blocking request/reply with timeout; a dead device daemon surfaces as
    // a management-plane error instead of a hang.
    ControlResult call(const char* action, const std::string& name = "",
                       int64_t a = 0, int64_t b = 0, int64_t c = 0,
                       int timeoutMs = 3000) {
        const uint64_t seq = ++seq_;
        onsim_ControlCommand cmd{};
        cmd.seq = seq;
        cmd.action = const_cast<char*>(action);
        cmd.name = const_cast<char*>(name.c_str());
        cmd.a = a; cmd.b = b; cmd.c = c;
        dds_write(writer_, &cmd);

        int waited = 0;
        while (waited < timeoutMs) {
            void* samples[1] = {nullptr};
            dds_sample_info_t info;
            int n = dds_take(reader_, samples, &info, 1, 1);
            if (n > 0 && info.valid_data) {
                auto* rep = static_cast<onsim_ControlReply*>(samples[0]);
                ControlResult res{rep->status, rep->error ? rep->error : ""};
                uint64_t got = rep->seq;
                dds_return_loan(reader_, samples, n);
                if (got == seq) return res;
                continue;  // stale reply from an earlier call; keep draining
            }
            if (n > 0) dds_return_loan(reader_, samples, n);
            dds_sleepfor(DDS_MSECS(10));
            waited += 10;
        }
        return ControlResult{-1, "device daemon unreachable (DDS timeout)"};
    }

private:
    dds_entity_t participant_;
    dds_entity_t writer_ = 0;
    dds_entity_t reader_ = 0;
    uint64_t seq_ = 0;
};

// Device-daemon side: poll commands, execute, reply.
class DdsControlServer {
public:
    using Handler = std::function<ControlResult(const onsim_ControlCommand&)>;

    explicit DdsControlServer(Handler handler)
        : participant_(makeParticipant()), handler_(std::move(handler)) {
        dds_entity_t cmdTopic = dds_create_topic(
            participant_, &onsim_ControlCommand_desc, "onsim_control_cmd",
            nullptr, nullptr);
        dds_entity_t repTopic = dds_create_topic(
            participant_, &onsim_ControlReply_desc, "onsim_control_reply",
            nullptr, nullptr);
        dds_qos_t* qos = reliableQos();
        reader_ = dds_create_reader(participant_, cmdTopic, qos, nullptr);
        writer_ = dds_create_writer(participant_, repTopic, qos, nullptr);
        dds_delete_qos(qos);
        if (writer_ < 0 || reader_ < 0) throw std::runtime_error("dds control server");
    }
    ~DdsControlServer() { dds_delete(participant_); }
    DdsControlServer(const DdsControlServer&) = delete;
    DdsControlServer& operator=(const DdsControlServer&) = delete;

    // Drain and execute all pending commands; returns how many were served.
    int poll() {
        int served = 0;
        for (;;) {
            void* samples[1] = {nullptr};
            dds_sample_info_t info;
            int n = dds_take(reader_, samples, &info, 1, 1);
            if (n <= 0) break;
            if (info.valid_data) {
                auto* cmd = static_cast<onsim_ControlCommand*>(samples[0]);
                ControlResult res = handler_(*cmd);
                onsim_ControlReply rep{};
                rep.seq = cmd->seq;
                rep.status = res.status;
                rep.error = const_cast<char*>(res.error.c_str());
                dds_write(writer_, &rep);
                ++served;
            }
            dds_return_loan(reader_, samples, n);
        }
        return served;
    }

private:
    dds_entity_t participant_;
    dds_entity_t reader_ = 0;
    dds_entity_t writer_ = 0;
    Handler handler_;
};

// Management-plane telemetry cache: subscribes to the telemetry topic and
// keeps the latest sample; NETCONF operational reads are served from here.
class DdsTelemetryCache {
public:
    DdsTelemetryCache() : participant_(makeParticipant()) {
        dds_entity_t topic = dds_create_topic(
            participant_, &onsim_TelemetryBlob_desc, "onsim_telemetry",
            nullptr, nullptr);
        dds_qos_t* qos = reliableQos();
        reader_ = dds_create_reader(participant_, topic, qos, nullptr);
        dds_delete_qos(qos);
        if (reader_ < 0) throw std::runtime_error("dds telemetry cache");
    }
    ~DdsTelemetryCache() { dds_delete(participant_); }
    DdsTelemetryCache(const DdsTelemetryCache&) = delete;
    DdsTelemetryCache& operator=(const DdsTelemetryCache&) = delete;

    // Drain any pending samples into the cache; true if we hold one.
    bool refresh() {
        for (;;) {
            void* samples[1] = {nullptr};
            dds_sample_info_t info;
            int n = dds_take(reader_, samples, &info, 1, 1);
            if (n <= 0) break;
            if (info.valid_data) {
                auto* blob = static_cast<onsim_TelemetryBlob*>(samples[0]);
                telemetry::TelemetrySample s;
                if (s.ParseFromArray(blob->payload._buffer,
                                     static_cast<int>(blob->payload._length))) {
                    latest_ = s;
                    have_ = true;
                }
            }
            dds_return_loan(reader_, samples, n);
        }
        return have_;
    }

    const telemetry::TelemetrySample& latest() const { return latest_; }

private:
    dds_entity_t participant_;
    dds_entity_t reader_ = 0;
    telemetry::TelemetrySample latest_;
    bool have_ = false;
};

// Device-daemon telemetry publisher (moved here from telemetry_dds.hpp).
class DdsTelemetryPublisher {
public:
    DdsTelemetryPublisher() : participant_(makeParticipant()) {
        dds_entity_t topic = dds_create_topic(
            participant_, &onsim_TelemetryBlob_desc, "onsim_telemetry",
            nullptr, nullptr);
        dds_qos_t* qos = reliableQos();
        writer_ = dds_create_writer(participant_, topic, qos, nullptr);
        dds_delete_qos(qos);
        if (writer_ < 0) throw std::runtime_error("dds telemetry publisher");
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
        blob.payload._release = false;
        dds_write(writer_, &blob);
    }

private:
    dds_entity_t participant_;
    dds_entity_t writer_ = 0;
};

}  // namespace onsim
