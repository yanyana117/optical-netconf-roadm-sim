// onsim-devd: the device daemon. Sole owner of the (simulated) optical
// hardware behind the C HAL. Serves provisioning commands arriving over the
// DDS control bus and publishes per-tick telemetry (Protocol Buffers
// payloads) over both DDS and ZeroMQ.
//
// Together with onsim-netconfd this forms the multi-component architecture
// real network elements use: management plane and device access are separate
// processes exchanging data over a pub/sub bus.

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

#include "dds_bus.hpp"
#include "onsim/hal.h"
#include "telemetry_pub.hpp"

namespace {

constexpr int kDegrees = 4;
std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

onsim::ControlResult execute(onsim_device* dev,
                             const onsim_ControlCommand& cmd) {
    const char* action = cmd.action ? cmd.action : "";
    const char* name = cmd.name ? cmd.name : "";
    onsim_status st = ONSIM_ERR_INVALID_ARG;
    if (std::strcmp(action, "xc_add") == 0) {
        st = onsim_xc_add(dev, name, static_cast<int>(cmd.a),
                          static_cast<int>(cmd.b), static_cast<int>(cmd.c));
    } else if (std::strcmp(action, "xc_del") == 0) {
        st = onsim_xc_delete(dev, name);
    } else if (std::strcmp(action, "xpdr_admin") == 0) {
        st = onsim_xpdr_set_admin(dev, static_cast<int>(cmd.a));
    } else if (std::strcmp(action, "xpdr_rate") == 0) {
        st = onsim_xpdr_set_rate(dev, static_cast<int>(cmd.a));
    } else if (std::strcmp(action, "xpdr_mod") == 0) {
        st = onsim_xpdr_set_modulation(dev, static_cast<int>(cmd.a));
    } else {
        return {ONSIM_ERR_INVALID_ARG, std::string("unknown action: ") + action};
    }
    if (st == ONSIM_OK) return {0, ""};
    return {static_cast<int64_t>(st), onsim_last_error(dev)};
}

}  // namespace

int main() {
    onsim_device* dev = onsim_create(kDegrees, 2026);
    if (dev == nullptr) {
        std::fprintf(stderr, "onsim-devd: failed to create device\n");
        return 1;
    }

    onsim::DdsControlServer server(
        [dev](const onsim_ControlCommand& cmd) { return execute(dev, cmd); });
    onsim::DdsTelemetryPublisher ddsTelemetry;
    onsim::TelemetryPublisher zmqTelemetry("tcp://*:5556");

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::printf("onsim-devd: %d-degree ROADM + transponder; DDS control bus up, "
                "telemetry on DDS topic onsim_telemetry + tcp://*:5556\n",
                kDegrees);

    uint64_t tick = 0;
    auto lastTick = std::chrono::steady_clock::now();
    while (g_running) {
        server.poll();
        auto now = std::chrono::steady_clock::now();
        if (now - lastTick >= std::chrono::seconds(1)) {
            lastTick = now;
            onsim_tick(dev);
            ++tick;
            const std::string payload =
                onsim::serializeTelemetry(dev, kDegrees, tick);
            ddsTelemetry.publish(tick, payload);
            zmqTelemetry.publish(payload);
        }
        dds_sleepfor(DDS_MSECS(10));
    }

    onsim_destroy(dev);
    return 0;
}
