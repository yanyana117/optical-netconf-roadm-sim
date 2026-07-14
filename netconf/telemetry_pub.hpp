// Telemetry serialization + ZeroMQ transport. The Protocol Buffers sample is
// built once per tick (serializeTelemetry) and handed to whichever transports
// are compiled in (ZeroMQ pub/sub here, DDS in telemetry_dds.hpp).
#pragma once

#include <string>

#include <zmq.hpp>

#include "onsim/hal.h"
#include "onsim_telemetry.pb.h"

namespace onsim {

inline std::string serializeTelemetry(onsim_device* dev, int degrees,
                                      uint64_t tick) {
    telemetry::TelemetrySample sample;
    sample.set_tick(tick);
    for (int p = 1; p <= degrees; ++p) {
        onsim_port_state st{};
        if (onsim_port_get(dev, p, &st) != ONSIM_OK) continue;
        telemetry::PortSample* ps = sample.add_ports();
        ps->set_number(static_cast<uint32_t>(p));
        ps->set_enabled(st.enabled != 0);
        ps->set_input_power_dbm(st.input_power_dbm);
        ps->set_output_power_dbm(st.output_power_dbm);
        ps->set_los_alarm(st.los_alarm != 0);
    }
    onsim_xpdr_state xs{};
    if (onsim_xpdr_get(dev, &xs) == ONSIM_OK) {
        telemetry::TransponderSample* ts = sample.mutable_transponder();
        ts->set_admin_up(xs.admin_up != 0);
        ts->set_line_rate_gbps(static_cast<uint32_t>(xs.line_rate_gbps));
        ts->set_modulation(
            static_cast<telemetry::TransponderSample::Modulation>(xs.modulation));
        ts->set_osnr_db(xs.osnr_db);
        ts->set_pre_fec_ber(xs.pre_fec_ber);
        ts->set_ber_degrade_alarm(xs.ber_degrade_alarm != 0);
    }
    std::string payload;
    sample.SerializeToString(&payload);
    return payload;
}

class TelemetryPublisher {
public:
    explicit TelemetryPublisher(const std::string& endpoint)
        : ctx_(1), sock_(ctx_, zmq::socket_type::pub) {
        sock_.bind(endpoint);
    }

    void publish(const std::string& payload) {
        sock_.send(zmq::const_buffer("telemetry", 9), zmq::send_flags::sndmore);
        sock_.send(zmq::buffer(payload), zmq::send_flags::none);
    }

private:
    zmq::context_t ctx_;
    zmq::socket_t sock_;
};

}  // namespace onsim
