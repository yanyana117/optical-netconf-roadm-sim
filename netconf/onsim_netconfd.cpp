// onsim-netconfd: bridges the sysrepo datastore (fronted by the Netopeer2
// NETCONF server) to the simulated optical hardware behind the C HAL.
//
// Architecture: declarative reconciliation. On every configuration
// transaction (SR_EV_CHANGE) the daemon reads the candidate running config
// and reconciles the device to it; a HAL rejection (e.g. wavelength
// collision) fails the transaction, so the NETCONF client gets a clean
// rpc-error and the datastore never diverges from hardware. On SR_EV_ABORT
// the device is reconciled back to the pre-change config.
//
// Operational state (port powers, alarms, BER) is served live from the HAL
// through operational get callbacks; a background thread ticks the
// simulation once per second.

#include <sysrepo.h>
#include <libyang/libyang.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "onsim/hal.h"

#ifdef ONSIM_TELEMETRY
#include "telemetry_pub.hpp"
#endif

namespace {

constexpr const char* kModule = "onsim-device";
constexpr int kDegrees = 4;

onsim_device* g_dev = nullptr;
std::atomic<bool> g_running{true};

struct XcConfig {
    int inPort = 0;
    int outPort = 0;
    int channel = 0;
};

struct DeviceConfig {
    std::map<std::string, XcConfig> xcs;
    bool adminUp = false;
    int rateGbps = 100;
    int modulation = 0;  // 0 qpsk, 1 qam8, 2 qam16
};

int leafInt(const lyd_node* node) {
    return std::atoi(lyd_get_value(node));
}

// Read the full onsim-device config subtree from a sysrepo session.
DeviceConfig readConfig(sr_session_ctx_t* session) {
    DeviceConfig cfg;
    sr_data_t* data = nullptr;
    if (sr_get_data(session, "/onsim-device:device//.", 0, 0, 0, &data) !=
            SR_ERR_OK || data == nullptr) {
        return cfg;
    }
    struct lyd_node* elem = nullptr;
    struct lyd_node* tree = data->tree;
    LYD_TREE_DFS_BEGIN(tree, elem) {
        const char* name = elem->schema ? elem->schema->name : "";
        if (std::strcmp(name, "cross-connect") == 0) {
            XcConfig xc;
            std::string key;
            for (lyd_node* child = lyd_child(elem); child != nullptr;
                 child = child->next) {
                const char* cname = child->schema->name;
                if (std::strcmp(cname, "name") == 0) key = lyd_get_value(child);
                else if (std::strcmp(cname, "in-port") == 0) xc.inPort = leafInt(child);
                else if (std::strcmp(cname, "out-port") == 0) xc.outPort = leafInt(child);
                else if (std::strcmp(cname, "channel") == 0) xc.channel = leafInt(child);
            }
            if (!key.empty()) cfg.xcs[key] = xc;
        } else if (std::strcmp(name, "admin-state") == 0) {
            cfg.adminUp = std::strcmp(lyd_get_value(elem), "up") == 0;
        } else if (std::strcmp(name, "line-rate") == 0) {
            const char* v = lyd_get_value(elem);
            cfg.rateGbps = std::strcmp(v, "r400g") == 0 ? 400
                         : std::strcmp(v, "r200g") == 0 ? 200 : 100;
        } else if (std::strcmp(name, "modulation") == 0) {
            const char* v = lyd_get_value(elem);
            cfg.modulation = std::strcmp(v, "qam16") == 0 ? 2
                           : std::strcmp(v, "qam8") == 0 ? 1 : 0;
        }
        LYD_TREE_DFS_END(tree, elem);
    }
    sr_release_data(data);
    return cfg;
}

// Drive the HAL to match `cfg`. Returns an empty string on success or a
// human-readable error (which fails the NETCONF transaction).
std::string reconcile(const DeviceConfig& cfg) {
    // --- transponder: sequence admin-down -> rate/mod -> desired admin ---
    onsim_xpdr_state xs{};
    onsim_xpdr_get(g_dev, &xs);
    if (xs.line_rate_gbps != cfg.rateGbps || xs.modulation != cfg.modulation) {
        onsim_xpdr_set_admin(g_dev, 0);
        if (onsim_xpdr_set_rate(g_dev, cfg.rateGbps) != ONSIM_OK)
            return std::string("line-rate: ") + onsim_last_error(g_dev);
        if (onsim_xpdr_set_modulation(g_dev, cfg.modulation) != ONSIM_OK)
            return std::string("modulation: ") + onsim_last_error(g_dev);
    }
    onsim_xpdr_set_admin(g_dev, cfg.adminUp ? 1 : 0);

    // --- cross-connects: delete stale, then add missing ---
    // The HAL has no bulk-read of XCs by name with params, so the daemon
    // tracks desired state directly: delete names not in cfg, re-add changed.
    static std::map<std::string, XcConfig> applied;  // daemon-lifetime cache
    for (auto it = applied.begin(); it != applied.end();) {
        auto want = cfg.xcs.find(it->first);
        bool changed = want == cfg.xcs.end() ||
                       std::memcmp(&want->second, &it->second, sizeof(XcConfig)) != 0;
        if (changed) {
            onsim_xc_delete(g_dev, it->first.c_str());
            it = applied.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [name, xc] : cfg.xcs) {
        if (applied.count(name) != 0) continue;
        onsim_status st = onsim_xc_add(g_dev, name.c_str(), xc.inPort,
                                       xc.outPort, xc.channel);
        if (st != ONSIM_OK) {
            return "cross-connect '" + name + "': " + onsim_last_error(g_dev);
        }
        applied[name] = xc;
    }
    return "";
}

int configChangeCb(sr_session_ctx_t* session, uint32_t /*sub_id*/,
                   const char* /*module*/, const char* /*xpath*/,
                   sr_event_t event, uint32_t /*request_id*/,
                   void* /*private_data*/) {
    if (event != SR_EV_CHANGE && event != SR_EV_ABORT) return SR_ERR_OK;
    // In CHANGE the session exposes the candidate config; in ABORT it
    // exposes the pre-change config — the same reconcile handles rollback.
    std::string err = reconcile(readConfig(session));
    if (!err.empty() && event == SR_EV_CHANGE) {
        sr_session_set_error_message(session, "%s", err.c_str());
        return SR_ERR_OPERATION_FAILED;
    }
    return SR_ERR_OK;
}

int addLeaf(const ly_ctx* ctx, lyd_node** parent, const std::string& path,
            const std::string& value) {
    lyd_node* out = nullptr;
    if (*parent == nullptr) {
        if (lyd_new_path(nullptr, ctx, path.c_str(), value.c_str(), 0, &out) !=
            LY_SUCCESS) return 1;
        *parent = out;
        return 0;
    }
    return lyd_new_path(*parent, nullptr, path.c_str(), value.c_str(), 0,
                        nullptr) == LY_SUCCESS ? 0 : 1;
}

std::string fmt(double v, int digits) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", digits, v);
    return buf;
}

int roadmOperCb(sr_session_ctx_t* session, uint32_t, const char*, const char*,
                const char*, uint32_t, lyd_node** parent, void*) {
    const ly_ctx* ctx = sr_session_acquire_context(session);
    int rc = 0;
    rc += addLeaf(ctx, parent, "/onsim-device:device/roadm/degrees",
                  std::to_string(kDegrees));
    for (int p = 1; p <= kDegrees && rc == 0; ++p) {
        onsim_port_state st{};
        if (onsim_port_get(g_dev, p, &st) != ONSIM_OK) continue;
        std::string base =
            "/onsim-device:device/roadm/port[number='" + std::to_string(p) + "']";
        rc += addLeaf(ctx, parent, base + "/enabled", st.enabled ? "true" : "false");
        rc += addLeaf(ctx, parent, base + "/input-power", fmt(st.input_power_dbm, 2));
        rc += addLeaf(ctx, parent, base + "/output-power", fmt(st.output_power_dbm, 2));
        rc += addLeaf(ctx, parent, base + "/los-alarm", st.los_alarm ? "true" : "false");
    }
    sr_session_release_context(session);
    return rc == 0 ? SR_ERR_OK : SR_ERR_INTERNAL;
}

int xpdrOperCb(sr_session_ctx_t* session, uint32_t, const char*, const char*,
               const char*, uint32_t, lyd_node** parent, void*) {
    const ly_ctx* ctx = sr_session_acquire_context(session);
    onsim_xpdr_state xs{};
    onsim_xpdr_get(g_dev, &xs);
    double ber = xs.pre_fec_ber;
    if (ber > 0 && ber < 1e-18) ber = 1e-18;  // decimal64 fraction-digits 18 floor
    int rc = 0;
    const std::string base = "/onsim-device:device/transponder/state";
    rc += addLeaf(ctx, parent, base + "/osnr", fmt(xs.osnr_db, 2));
    rc += addLeaf(ctx, parent, base + "/pre-fec-ber", fmt(ber, 18));
    rc += addLeaf(ctx, parent, base + "/ber-degrade-alarm",
                  xs.ber_degrade_alarm ? "true" : "false");
    sr_session_release_context(session);
    return rc == 0 ? SR_ERR_OK : SR_ERR_INTERNAL;
}

void onSignal(int) { g_running = false; }

}  // namespace

int main() {
    g_dev = onsim_create(kDegrees, 2026);
    if (g_dev == nullptr) {
        std::fprintf(stderr, "failed to create device\n");
        return 1;
    }

    sr_conn_ctx_t* conn = nullptr;
    sr_session_ctx_t* session = nullptr;
    sr_subscription_ctx_t* sub = nullptr;
    if (sr_connect(0, &conn) != SR_ERR_OK ||
        sr_session_start(conn, SR_DS_RUNNING, &session) != SR_ERR_OK) {
        std::fprintf(stderr, "failed to connect to sysrepo\n");
        return 1;
    }

    // Apply whatever is already in the running datastore, then subscribe.
    std::string err = reconcile(readConfig(session));
    if (!err.empty()) std::fprintf(stderr, "startup reconcile: %s\n", err.c_str());

    int rc = sr_module_change_subscribe(session, kModule, "/onsim-device:device",
                                        configChangeCb, nullptr, 0, 0, &sub);
    rc |= sr_oper_get_subscribe(session, kModule, "/onsim-device:device/roadm",
                                roadmOperCb, nullptr, 0, &sub);
    rc |= sr_oper_get_subscribe(session, kModule,
                                "/onsim-device:device/transponder/state",
                                xpdrOperCb, nullptr, 0, &sub);
    if (rc != SR_ERR_OK) {
        std::fprintf(stderr, "subscribe failed: %d\n", rc);
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

#ifdef ONSIM_TELEMETRY
    onsim::TelemetryPublisher telemetry("tcp://*:5556", kDegrees);
    std::printf("onsim-netconfd: telemetry PUB on tcp://*:5556\n");
#endif
    std::printf("onsim-netconfd: %d-degree ROADM + transponder ready\n", kDegrees);

    uint64_t tick = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        onsim_tick(g_dev);
        ++tick;
#ifdef ONSIM_TELEMETRY
        telemetry.publish(g_dev, tick);
#endif
    }

    sr_unsubscribe(sub);
    sr_session_stop(session);
    sr_disconnect(conn);
    onsim_destroy(g_dev);
    return 0;
}
