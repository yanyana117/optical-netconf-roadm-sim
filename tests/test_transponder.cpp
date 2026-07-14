#include "onsim/transponder.hpp"

#include <gtest/gtest.h>

using onsim::AdminState;
using onsim::LineRate;
using onsim::Modulation;
using onsim::Transponder;

namespace {

TEST(Transponder, RequiredOsnrOrdering) {
    // Denser modulation and faster rates must require more OSNR.
    EXPECT_LT(Transponder::requiredOsnrDb(Modulation::kQpsk, LineRate::k100G),
              Transponder::requiredOsnrDb(Modulation::k8Qam, LineRate::k100G));
    EXPECT_LT(Transponder::requiredOsnrDb(Modulation::k8Qam, LineRate::k100G),
              Transponder::requiredOsnrDb(Modulation::k16Qam, LineRate::k100G));
    EXPECT_LT(Transponder::requiredOsnrDb(Modulation::kQpsk, LineRate::k100G),
              Transponder::requiredOsnrDb(Modulation::kQpsk, LineRate::k400G));
}

TEST(Transponder, ReconfigRequiresAdminDown) {
    Transponder t;
    ASSERT_TRUE(t.setAdminState(AdminState::kUp));
    EXPECT_FALSE(t.setLineRate(LineRate::k400G));
    EXPECT_FALSE(t.setModulation(Modulation::k16Qam));
    EXPECT_NE(t.lastError().find("admin"), std::string::npos);
    ASSERT_TRUE(t.setAdminState(AdminState::kDown));
    EXPECT_TRUE(t.setLineRate(LineRate::k400G));
    EXPECT_TRUE(t.setModulation(Modulation::k16Qam));
}

TEST(Transponder, BerGrowsMonotonicallyAsOsnrMarginShrinks) {
    Transponder t;
    t.setAdminState(AdminState::kUp);
    t.setOsnr(30.0);  // huge margin for QPSK@100G (needs 12 dB)
    const double cleanBer = t.state().preFecBer;
    t.setOsnr(20.0);  // 8 dB margin
    const double midBer = t.state().preFecBer;
    t.setOsnr(14.0);  // 2 dB margin: degraded but still under the FEC ceiling
    const double dirtyBer = t.state().preFecBer;
    EXPECT_LT(cleanBer, midBer);
    EXPECT_LT(midBer, dirtyBer);
    EXPECT_FALSE(t.state().berDegradeAlarm);
}

TEST(Transponder, DegradeAlarmCrossesFecThreshold) {
    Transponder t;
    t.setAdminState(AdminState::kUp);
    t.setOsnr(30.0);
    EXPECT_FALSE(t.state().berDegradeAlarm);
    t.setOsnr(10.0);  // below required OSNR entirely
    EXPECT_TRUE(t.state().berDegradeAlarm);
    EXPECT_GT(t.state().preFecBer, Transponder::kBerAlarmThreshold);
}

TEST(Transponder, AdminDownIsSilent) {
    Transponder t;
    t.setOsnr(5.0);
    EXPECT_EQ(t.state().preFecBer, 0.0);
    EXPECT_FALSE(t.state().berDegradeAlarm);
}

TEST(Transponder, TickIsDeterministicPerSeed) {
    Transponder a(11), b(11);
    a.setAdminState(AdminState::kUp);
    b.setAdminState(AdminState::kUp);
    for (int i = 0; i < 50; ++i) {
        a.tick();
        b.tick();
    }
    EXPECT_DOUBLE_EQ(a.state().osnrDb, b.state().osnrDb);
    EXPECT_EQ(a.state().ticks, 50u);
}

}  // namespace
