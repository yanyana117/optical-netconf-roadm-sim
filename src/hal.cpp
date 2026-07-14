#include "onsim/hal.h"

#include <memory>
#include <string>

#include "onsim/roadm.hpp"
#include "onsim/transponder.hpp"

struct onsim_device {
    onsim::RoadmDevice roadm;
    onsim::Transponder xpdr;
    std::string lastError;

    onsim_device(int degrees, uint64_t seed)
        : roadm(degrees, 6.0, -28.0, seed), xpdr(seed + 1) {}
};

extern "C" {

onsim_device* onsim_create(int degrees, uint64_t seed) {
    if (degrees < 2) return nullptr;
    return new (std::nothrow) onsim_device(degrees, seed);
}

void onsim_destroy(onsim_device* d) { delete d; }

onsim_status onsim_xc_add(onsim_device* d, const char* name, int in_port,
                          int out_port, int channel) {
    if (d == nullptr || name == nullptr) return ONSIM_ERR_INVALID_ARG;
    onsim::CrossConnect xc;
    xc.name = name;
    xc.inPort = in_port;
    xc.outPort = out_port;
    xc.channel = channel;
    if (d->roadm.addCrossConnect(xc)) return ONSIM_OK;
    d->lastError = d->roadm.lastError();
    return d->lastError.find("collision") != std::string::npos
               ? ONSIM_ERR_COLLISION
               : ONSIM_ERR_INVALID_ARG;
}

onsim_status onsim_xc_delete(onsim_device* d, const char* name) {
    if (d == nullptr || name == nullptr) return ONSIM_ERR_INVALID_ARG;
    if (d->roadm.deleteCrossConnect(name)) return ONSIM_OK;
    d->lastError = d->roadm.lastError();
    return ONSIM_ERR_NOT_FOUND;
}

onsim_status onsim_port_enable(onsim_device* d, int port, int enabled) {
    if (d == nullptr) return ONSIM_ERR_INVALID_ARG;
    if (d->roadm.setPortEnabled(port, enabled != 0)) return ONSIM_OK;
    d->lastError = d->roadm.lastError();
    return ONSIM_ERR_INVALID_ARG;
}

onsim_status onsim_port_set_input_power(onsim_device* d, int port, double dbm) {
    if (d == nullptr) return ONSIM_ERR_INVALID_ARG;
    if (d->roadm.setInputPower(port, dbm)) return ONSIM_OK;
    d->lastError = d->roadm.lastError();
    return ONSIM_ERR_INVALID_ARG;
}

onsim_status onsim_port_get(onsim_device* d, int port, onsim_port_state* out) {
    if (d == nullptr || out == nullptr) return ONSIM_ERR_INVALID_ARG;
    auto st = d->roadm.portState(port);
    if (!st) {
        d->lastError = "invalid port";
        return ONSIM_ERR_NOT_FOUND;
    }
    out->enabled = st->enabled ? 1 : 0;
    out->input_power_dbm = st->inputPowerDbm;
    out->output_power_dbm = st->outputPowerDbm;
    out->los_alarm = st->losAlarm ? 1 : 0;
    return ONSIM_OK;
}

onsim_status onsim_xpdr_set_admin(onsim_device* d, int up) {
    if (d == nullptr) return ONSIM_ERR_INVALID_ARG;
    d->xpdr.setAdminState(up != 0 ? onsim::AdminState::kUp
                                  : onsim::AdminState::kDown);
    return ONSIM_OK;
}

onsim_status onsim_xpdr_set_rate(onsim_device* d, int gbps) {
    if (d == nullptr) return ONSIM_ERR_INVALID_ARG;
    onsim::LineRate r;
    switch (gbps) {
        case 100: r = onsim::LineRate::k100G; break;
        case 200: r = onsim::LineRate::k200G; break;
        case 400: r = onsim::LineRate::k400G; break;
        default: d->lastError = "unsupported line rate"; return ONSIM_ERR_INVALID_ARG;
    }
    if (d->xpdr.setLineRate(r)) return ONSIM_OK;
    d->lastError = d->xpdr.lastError();
    return ONSIM_ERR_BUSY;
}

onsim_status onsim_xpdr_set_modulation(onsim_device* d, int modulation) {
    if (d == nullptr) return ONSIM_ERR_INVALID_ARG;
    onsim::Modulation m;
    switch (modulation) {
        case 0: m = onsim::Modulation::kQpsk; break;
        case 1: m = onsim::Modulation::k8Qam; break;
        case 2: m = onsim::Modulation::k16Qam; break;
        default: d->lastError = "unsupported modulation"; return ONSIM_ERR_INVALID_ARG;
    }
    if (d->xpdr.setModulation(m)) return ONSIM_OK;
    d->lastError = d->xpdr.lastError();
    return ONSIM_ERR_BUSY;
}

onsim_status onsim_xpdr_set_osnr(onsim_device* d, double db) {
    if (d == nullptr) return ONSIM_ERR_INVALID_ARG;
    d->xpdr.setOsnr(db);
    return ONSIM_OK;
}

onsim_status onsim_xpdr_get(onsim_device* d, onsim_xpdr_state* out) {
    if (d == nullptr || out == nullptr) return ONSIM_ERR_INVALID_ARG;
    const onsim::TransponderState& s = d->xpdr.state();
    out->admin_up = s.admin == onsim::AdminState::kUp ? 1 : 0;
    switch (s.rate) {
        case onsim::LineRate::k100G: out->line_rate_gbps = 100; break;
        case onsim::LineRate::k200G: out->line_rate_gbps = 200; break;
        case onsim::LineRate::k400G: out->line_rate_gbps = 400; break;
    }
    out->modulation = static_cast<int>(s.mod);
    out->osnr_db = s.osnrDb;
    out->pre_fec_ber = s.preFecBer;
    out->ber_degrade_alarm = s.berDegradeAlarm ? 1 : 0;
    return ONSIM_OK;
}

void onsim_tick(onsim_device* d) {
    if (d == nullptr) return;
    d->roadm.tick();
    d->xpdr.tick();
}

const char* onsim_last_error(onsim_device* d) {
    return d == nullptr ? "null device" : d->lastError.c_str();
}

}  // extern "C"
