// onsim-netconfd: the management-plane daemon. Bridges the sysrepo datastore
// (fronted by the Netopeer2 NETCONF server) to the device daemon
// (onsim-devd) over the NE-internal DDS bus. This process never touches the
// hardware abstraction layer directly:
//
//   - configuration: declarative reconciliation. On SR_EV_CHANGE the
//     candidate config is read and pushed to onsim-devd as DDS
//     request/reply commands; a device NACK (or an unreachable device
//     daemon) fails the NETCONF transaction with a clean rpc-error.
//     SR_EV_ABORT reconciles back to the pre-change config.
//   - operational state: served from a cache of the latest DDS telemetry
//     sample published by onsim-devd, the way real NEs answer state reads
//     without a round trip to hardware.

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

#include "dds_bus.hpp"

namespace {

constexpr const char* kModule = "onsim-device";

onsim::DdsControlClient* g_bus = nullptr;
onsim::DdsTelemetryCache* g_cache = nullptr;
std::atomic<bool> g_running{true};

struct XcConfig {
    int64_t inPort = 0;
    int64_t outPort = 0;
    int64_t channel = 0;
    bool operator==(const XcConfig& o) const {
        return inPort == o.inPort && outPort == o.outPort && channel == o.channel;
    }
};

struct DeviceConfig {
    std::map<std::string, XcConfig> xcs;
    bool adminUp = false;
    int64_t rateGbps = 100;
    int64_t modulation = 0;  // 0 qpsk, 1 qam8, 2 qam16
};

int64_t leafInt(const lyd_node* node) {
    return std::atoll(lyd_get_value(node));
}

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

// Drive the device (over the DDS bus) to match `cfg`.
std::string reconcile(const DeviceConfig& cfg) {
    static std::map<std::string, XcConfig> applied;   // last config we pushed
    static int64_t appliedRate = 100, appliedMod = 0;

    // Transponder: sequence rate/modulation changes through admin-down, the
    // way NE management planes do, then set the desired admin state.
    if (appliedRate != cfg.rateGbps || appliedMod != cfg.modulation) {
        g_bus->call("xpdr_admin", "", 0);
        onsim::ControlResult r = g_bus->call("xpdr_rate", "", cfg.rateGbps);
        if (!r.ok()) return "line-rate: " + r.error;
        r = g_bus->call("xpdr_mod", "", cfg.modulation);
        if (!r.ok()) return "modulation: " + r.error;
        appliedRate = cfg.rateGbps;
        appliedMod = cfg.modulation;
    }
    onsim::ControlResult r = g_bus->call("xpdr_admin", "", cfg.adminUp ? 1 : 0);
    if (!r.ok()) return "admin-state: " + r.error;

    // Cross-connects: delete stale, then add missing.
    for (auto it = applied.begin(); it != applied.end();) {
        auto want = cfg.xcs.find(it->first);
        if (want == cfg.xcs.end() || !(want->second == it->second)) {
            g_bus->call("xc_del", it->first);
            it = applied.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [name, xc] : cfg.xcs) {
        if (applied.count(name) != 0) continue;
        r = g_bus->call("xc_add", name, xc.inPort, xc.outPort, xc.channel);
        if (!r.ok()) return "cross-connect '" + name + "': " + r.error;
        applied[name] = xc;
    }
    return "";
}

int configChangeCb(sr_session_ctx_t* session, uint32_t, const char*,
                   const char*, sr_event_t event, uint32_t, void*) {
    if (event != SR_EV_CHANGE && event != SR_EV_ABORT) return SR_ERR_OK;
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

// Wait briefly for the first telemetry sample so early reads don't miss.
bool cacheReady() {
    for (int i = 0; i < 30; ++i) {
        if (g_cache->refresh()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return false;
}

int roadmOperCb(sr_session_ctx_t* session, uint32_t, const char*, const char*,
                const char*, uint32_t, lyd_node** parent, void*) {
    if (!cacheReady()) return SR_ERR_OK;
    const onsim::telemetry::TelemetrySample& s = g_cache->latest();
    const ly_ctx* ctx = sr_session_acquire_context(session);
    int rc = addLeaf(ctx, parent, "/onsim-device:device/roadm/degrees",
                     std::to_string(s.ports_size()));
    for (const auto& p : s.ports()) {
        std::string base = "/onsim-device:device/roadm/port[number='" +
                           std::to_string(p.number()) + "']";
        rc += addLeaf(ctx, parent, base + "/enabled", p.enabled() ? "true" : "false");
        rc += addLeaf(ctx, parent, base + "/input-power", fmt(p.input_power_dbm(), 2));
        rc += addLeaf(ctx, parent, base + "/output-power", fmt(p.output_power_dbm(), 2));
        rc += addLeaf(ctx, parent, base + "/los-alarm", p.los_alarm() ? "true" : "false");
    }
    sr_session_release_context(session);
    return rc == 0 ? SR_ERR_OK : SR_ERR_INTERNAL;
}

int xpdrOperCb(sr_session_ctx_t* session, uint32_t, const char*, const char*,
               const char*, uint32_t, lyd_node** parent, void*) {
    if (!cacheReady()) return SR_ERR_OK;
    const auto& xp = g_cache->latest().transponder();
    const ly_ctx* ctx = sr_session_acquire_context(session);
    double ber = xp.pre_fec_ber();
    if (ber > 0 && ber < 1e-18) ber = 1e-18;  // decimal64 fraction-digits floor
    const std::string base = "/onsim-device:device/transponder/state";
    int rc = addLeaf(ctx, parent, base + "/osnr", fmt(xp.osnr_db(), 2));
    rc += addLeaf(ctx, parent, base + "/pre-fec-ber", fmt(ber, 18));
    rc += addLeaf(ctx, parent, base + "/ber-degrade-alarm",
                  xp.ber_degrade_alarm() ? "true" : "false");
    sr_session_release_context(session);
    return rc == 0 ? SR_ERR_OK : SR_ERR_INTERNAL;
}

void onSignal(int) { g_running = false; }

}  // namespace

int main() {
    static onsim::DdsControlClient bus;
    static onsim::DdsTelemetryCache cache;
    g_bus = &bus;
    g_cache = &cache;

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
    std::printf("onsim-netconfd: management plane up (device access via DDS bus)\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        cache.refresh();
    }

    sr_unsubscribe(sub);
    sr_session_stop(session);
    sr_disconnect(conn);
    return 0;
}
