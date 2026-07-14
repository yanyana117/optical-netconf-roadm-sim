#include "onsim/roadm.hpp"

#include "onsim/wavelength_grid.hpp"

namespace onsim {
namespace {

// xorshift64: tiny deterministic PRNG so simulations are reproducible.
uint64_t nextRand(uint64_t& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

// Uniform drift in [-0.05, +0.05] dB per tick.
double drift(uint64_t& s) {
    return (static_cast<double>(nextRand(s) % 1000) / 1000.0 - 0.5) * 0.1;
}

}  // namespace

RoadmDevice::RoadmDevice(int degrees, double insertionLossDb,
                         double losThresholdDbm, uint64_t seed)
    : degrees_(degrees),
      insertionLossDb_(insertionLossDb),
      losThresholdDbm_(losThresholdDbm),
      rngState_(seed == 0 ? 1 : seed) {
    for (int p = 1; p <= degrees_; ++p) ports_[p] = PortState{};
    reevaluate();
}

bool RoadmDevice::addCrossConnect(const CrossConnect& xc) {
    if (!validPort(xc.inPort) || !validPort(xc.outPort)) {
        lastError_ = "invalid port";
        return false;
    }
    if (xc.inPort == xc.outPort) {
        lastError_ = "input and output port must differ";
        return false;
    }
    if (!WavelengthGrid::isValidChannel(xc.channel)) {
        lastError_ = "invalid wavelength channel";
        return false;
    }
    if (xc.name.empty() || xcByName_.count(xc.name) != 0) {
        lastError_ = "cross-connect name empty or already in use";
        return false;
    }
    for (const auto& [name, other] : xcByName_) {
        if (other.channel == xc.channel && other.outPort == xc.outPort) {
            lastError_ = "wavelength collision on output port";
            return false;
        }
        if (other.channel == xc.channel && other.inPort == xc.inPort) {
            lastError_ = "channel already consumed on input port";
            return false;
        }
    }
    xcByName_[xc.name] = xc;
    reevaluate();
    return true;
}

bool RoadmDevice::deleteCrossConnect(const std::string& name) {
    if (xcByName_.erase(name) == 0) {
        lastError_ = "no such cross-connect";
        return false;
    }
    reevaluate();
    return true;
}

std::vector<CrossConnect> RoadmDevice::crossConnects() const {
    std::vector<CrossConnect> out;
    out.reserve(xcByName_.size());
    for (const auto& [name, xc] : xcByName_) out.push_back(xc);
    return out;
}

bool RoadmDevice::setPortEnabled(int port, bool enabled) {
    if (!validPort(port)) {
        lastError_ = "invalid port";
        return false;
    }
    ports_[port].enabled = enabled;
    reevaluate();
    return true;
}

bool RoadmDevice::setInputPower(int port, double dbm) {
    if (!validPort(port)) {
        lastError_ = "invalid port";
        return false;
    }
    ports_[port].inputPowerDbm = dbm;
    reevaluate();
    return true;
}

std::optional<PortState> RoadmDevice::portState(int port) const {
    auto it = ports_.find(port);
    if (it == ports_.end()) return std::nullopt;
    return it->second;
}

void RoadmDevice::tick() {
    for (auto& [port, st] : ports_) st.inputPowerDbm += drift(rngState_);
    reevaluate();
}

void RoadmDevice::reevaluate() {
    // Start every output dark; light it only if a cross-connect feeds it.
    for (auto& [port, st] : ports_) {
        st.outputPowerDbm = -60.0;
        st.losAlarm = false;
    }
    for (const auto& [name, xc] : xcByName_) {
        PortState& in = ports_[xc.inPort];
        PortState& out = ports_[xc.outPort];
        if (!in.enabled || !out.enabled) continue;
        double p = in.inputPowerDbm - insertionLossDb_;
        if (p > out.outputPowerDbm) out.outputPowerDbm = p;
    }
    for (auto& [port, st] : ports_) {
        bool carries = false;
        for (const auto& [name, xc] : xcByName_) {
            if (xc.outPort == port && st.enabled) carries = true;
        }
        st.losAlarm = carries && st.outputPowerDbm < losThresholdDbm_;
    }
}

}  // namespace onsim
