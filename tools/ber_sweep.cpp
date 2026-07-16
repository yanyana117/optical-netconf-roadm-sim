// Experiment: sweep received OSNR across every modulation x line-rate
// configuration and record the transponder model's pre-FEC BER.
// Output: CSV on stdout (modulation,rate_gbps,osnr_db,pre_fec_ber,alarm).
// Plotted by tools/plot_ber_sweep.py into docs/experiments/ber_vs_osnr.png.
#include <cstdio>

#include "onsim/transponder.hpp"

using onsim::AdminState;
using onsim::LineRate;
using onsim::Modulation;
using onsim::Transponder;

int main() {
    const Modulation mods[] = {Modulation::kQpsk, Modulation::k8Qam,
                               Modulation::k16Qam};
    const char* modNames[] = {"QPSK", "8QAM", "16QAM"};
    const LineRate rates[] = {LineRate::k100G, LineRate::k200G,
                              LineRate::k400G};
    const int rateGbps[] = {100, 200, 400};

    std::printf("modulation,rate_gbps,osnr_db,pre_fec_ber,alarm\n");
    for (int m = 0; m < 3; ++m) {
        for (int r = 0; r < 3; ++r) {
            Transponder t;
            t.setLineRate(rates[r]);
            t.setModulation(mods[m]);
            t.setAdminState(AdminState::kUp);
            for (double osnr = 8.0; osnr <= 32.0 + 1e-9; osnr += 0.25) {
                t.setOsnr(osnr);
                const auto& st = t.state();
                std::printf("%s,%d,%.2f,%.6e,%d\n", modNames[m], rateGbps[r],
                            osnr, st.preFecBer, st.berDegradeAlarm ? 1 : 0);
            }
        }
    }
    return 0;
}
