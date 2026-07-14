#include "onsim/transponder.hpp"

#include <cmath>

namespace onsim {
namespace {

uint64_t nextRand(uint64_t& s) {
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return s;
}

double drift(uint64_t& s) {
    return (static_cast<double>(nextRand(s) % 1000) / 1000.0 - 0.5) * 0.2;
}

}  // namespace

Transponder::Transponder(uint64_t seed) : rngState_(seed == 0 ? 1 : seed) {
    recomputeBer();
}

double Transponder::requiredOsnrDb(Modulation m, LineRate r) {
    // Baseline required OSNR for QPSK@100G, plus penalties for denser
    // modulation and higher symbol-rate configurations (textbook shape,
    // simplified numbers).
    double osnr = 12.0;
    switch (m) {
        case Modulation::kQpsk: break;
        case Modulation::k8Qam: osnr += 4.0; break;
        case Modulation::k16Qam: osnr += 7.0; break;
    }
    switch (r) {
        case LineRate::k100G: break;
        case LineRate::k200G: osnr += 3.0; break;
        case LineRate::k400G: osnr += 6.0; break;
    }
    return osnr;
}

bool Transponder::setAdminState(AdminState s) {
    state_.admin = s;
    recomputeBer();
    return true;
}

bool Transponder::setLineRate(LineRate r) {
    if (state_.admin == AdminState::kUp) {
        lastError_ = "line rate change requires admin state down";
        return false;
    }
    state_.rate = r;
    recomputeBer();
    return true;
}

bool Transponder::setModulation(Modulation m) {
    if (state_.admin == AdminState::kUp) {
        lastError_ = "modulation change requires admin state down";
        return false;
    }
    state_.mod = m;
    recomputeBer();
    return true;
}

void Transponder::setOsnr(double db) {
    state_.osnrDb = db;
    recomputeBer();
}

void Transponder::tick() {
    state_.osnrDb += drift(rngState_);
    ++state_.ticks;
    recomputeBer();
}

void Transponder::recomputeBer() {
    if (state_.admin == AdminState::kDown) {
        state_.preFecBer = 0.0;
        state_.berDegradeAlarm = false;
        return;
    }
    // Margin in dB between received and required OSNR. At +6 dB margin the
    // link is clean (~1e-6); every lost dB costs roughly a decade until the
    // SD-FEC ceiling is crossed.
    const double margin = state_.osnrDb - requiredOsnrDb(state_.mod, state_.rate);
    const double exponent = -6.0 - (margin - 6.0);  // margin 6 -> 1e-6
    state_.preFecBer = std::min(0.5, std::pow(10.0, exponent));
    state_.berDegradeAlarm = state_.preFecBer > kBerAlarmThreshold;
}

}  // namespace onsim
