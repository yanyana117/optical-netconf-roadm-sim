// C++ DDS subscriber for the NE telemetry stream: receives TelemetryBlob
// samples over Cyclone DDS, parses the Protocol Buffers payload, prints a
// one-line summary per sample. Usage: onsim-dds-sub [num_samples]
#include <cstdio>
#include <cstdlib>

#include "dds/dds.h"
#include "onsim_dds.h"
#include "onsim_telemetry.pb.h"

int main(int argc, char** argv) {
    const int want = argc > 1 ? std::atoi(argv[1]) : 5;

    dds_entity_t participant =
        dds_create_participant(DDS_DOMAIN_DEFAULT, nullptr, nullptr);
    dds_entity_t topic = dds_create_topic(
        participant, &onsim_TelemetryBlob_desc, "onsim_telemetry", nullptr, nullptr);
    dds_qos_t* qos = dds_create_qos();
    dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
    dds_entity_t reader = dds_create_reader(participant, topic, qos, nullptr);
    dds_delete_qos(qos);
    if (reader < 0) {
        std::fprintf(stderr, "failed to create DDS reader\n");
        return 1;
    }
    std::printf("DDS subscriber up (topic onsim_telemetry, Cyclone DDS)\n");

    int got = 0, idleMs = 0;
    while (got < want && idleMs < 15000) {
        void* samples[1] = {nullptr};
        dds_sample_info_t info;
        int n = dds_take(reader, samples, &info, 1, 1);
        if (n <= 0 || !info.valid_data) {
            dds_sleepfor(DDS_MSECS(100));
            idleMs += 100;
            continue;
        }
        idleMs = 0;
        auto* blob = static_cast<onsim_TelemetryBlob*>(samples[0]);
        onsim::telemetry::TelemetrySample sample;
        if (sample.ParseFromArray(blob->payload._buffer,
                                  static_cast<int>(blob->payload._length))) {
            const auto& xp = sample.transponder();
            std::printf("dds tick=%llu ports=%d xpdr[%s %uG] osnr=%.2f ber=%.2e%s\n",
                        static_cast<unsigned long long>(sample.tick()),
                        sample.ports_size(), xp.admin_up() ? "up" : "down",
                        xp.line_rate_gbps(), xp.osnr_db(), xp.pre_fec_ber(),
                        xp.ber_degrade_alarm() ? " !DEGRADE" : "");
            ++got;
        }
        dds_return_loan(reader, samples, n);
    }
    dds_delete(participant);
    if (got < want) {
        std::fprintf(stderr, "timed out: %d/%d samples\n", got, want);
        return 1;
    }
    return 0;
}
