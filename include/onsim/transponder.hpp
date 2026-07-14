// Simulated coherent transponder: admin state, line rate, modulation format,
// and an OSNR-margin-driven pre-FEC BER model with a degrade alarm.
#pragma once

#include <cstdint>
#include <string>

namespace onsim {

enum class AdminState { kDown, kUp };
enum class LineRate { k100G, k200G, k400G };
enum class Modulation { kQpsk, k8Qam, k16Qam };

struct TransponderState {
    AdminState admin = AdminState::kDown;
    LineRate rate = LineRate::k100G;
    Modulation mod = Modulation::kQpsk;
    double osnrDb = 30.0;       // received OSNR
    double preFecBer = 0.0;     // simulated pre-FEC bit error rate
    bool berDegradeAlarm = false;
    uint64_t ticks = 0;
};

// Required OSNR rises with denser modulation; BER grows exponentially as the
// received OSNR margin shrinks. Post-FEC is assumed error-free while pre-FEC
// BER stays under `kBerAlarmThreshold` (typical SD-FEC limit ~2e-2).
class Transponder {
public:
    static constexpr double kBerAlarmThreshold = 2e-2;

    explicit Transponder(uint64_t seed = 7);

    bool setAdminState(AdminState s);
    // Rate/modulation changes are rejected while admin is Up (carrier-grade
    // devices require the port down for reconfiguration).
    bool setLineRate(LineRate r);
    bool setModulation(Modulation m);
    void setOsnr(double db);  // test hook / upstream impairment

    const TransponderState& state() const { return state_; }
    std::string lastError() const { return lastError_; }

    // Advance simulation: OSNR drifts slightly; BER recomputed from margin.
    void tick();

    static double requiredOsnrDb(Modulation m, LineRate r);

private:
    void recomputeBer();

    TransponderState state_;
    uint64_t rngState_;
    std::string lastError_;
};

}  // namespace onsim
