#include "onsim/roadm.hpp"

#include <gtest/gtest.h>

#include "onsim/wavelength_grid.hpp"

using onsim::CrossConnect;
using onsim::RoadmDevice;

namespace {

CrossConnect xc(const std::string& name, int in, int out, int ch) {
    CrossConnect x;
    x.name = name;
    x.inPort = in;
    x.outPort = out;
    x.channel = ch;
    return x;
}

TEST(WavelengthGrid, ChannelValidationAndFrequency) {
    EXPECT_TRUE(onsim::WavelengthGrid::isValidChannel(1));
    EXPECT_TRUE(onsim::WavelengthGrid::isValidChannel(96));
    EXPECT_FALSE(onsim::WavelengthGrid::isValidChannel(0));
    EXPECT_FALSE(onsim::WavelengthGrid::isValidChannel(97));
    EXPECT_DOUBLE_EQ(*onsim::WavelengthGrid::centerFrequencyThz(1), 191.35);
    EXPECT_DOUBLE_EQ(*onsim::WavelengthGrid::centerFrequencyThz(2), 191.40);
    EXPECT_FALSE(onsim::WavelengthGrid::centerFrequencyThz(200).has_value());
}

TEST(Roadm, AddCrossConnectComputesOutputPower) {
    RoadmDevice r(4);
    ASSERT_TRUE(r.setInputPower(1, -8.0));
    ASSERT_TRUE(r.addCrossConnect(xc("east-40", 1, 2, 40)));
    auto out = r.portState(2);
    ASSERT_TRUE(out.has_value());
    // -8 dBm in, 6 dB insertion loss -> -14 dBm out, no LOS.
    EXPECT_NEAR(out->outputPowerDbm, -14.0, 1e-9);
    EXPECT_FALSE(out->losAlarm);
}

TEST(Roadm, RejectsInvalidPortChannelAndDuplicates) {
    RoadmDevice r(2);
    EXPECT_FALSE(r.addCrossConnect(xc("bad-port", 1, 5, 10)));
    EXPECT_FALSE(r.addCrossConnect(xc("bad-ch", 1, 2, 0)));
    EXPECT_FALSE(r.addCrossConnect(xc("same-port", 1, 1, 10)));
    ASSERT_TRUE(r.addCrossConnect(xc("ok", 1, 2, 10)));
    EXPECT_FALSE(r.addCrossConnect(xc("ok", 2, 1, 11)));  // duplicate name
}

TEST(Roadm, DetectsWavelengthCollision) {
    RoadmDevice r(4);
    ASSERT_TRUE(r.addCrossConnect(xc("a", 1, 3, 20)));
    // Same channel to the same output port from another input: collision.
    EXPECT_FALSE(r.addCrossConnect(xc("b", 2, 3, 20)));
    EXPECT_NE(r.lastError().find("collision"), std::string::npos);
    // Same channel to a different output port is fine.
    EXPECT_TRUE(r.addCrossConnect(xc("c", 2, 4, 20)));
}

TEST(Roadm, LowPowerRaisesLosAlarmAndDeleteClearsIt) {
    RoadmDevice r(2);
    ASSERT_TRUE(r.setInputPower(1, -25.0));  // -25 - 6 = -31 < -28 threshold
    ASSERT_TRUE(r.addCrossConnect(xc("weak", 1, 2, 5)));
    ASSERT_TRUE(r.portState(2)->losAlarm);
    ASSERT_TRUE(r.deleteCrossConnect("weak"));
    EXPECT_FALSE(r.portState(2)->losAlarm);  // dark port carries no signal
    EXPECT_FALSE(r.deleteCrossConnect("weak"));
}

TEST(Roadm, DisabledPortDropsLight) {
    RoadmDevice r(2);
    ASSERT_TRUE(r.addCrossConnect(xc("x", 1, 2, 7)));
    ASSERT_TRUE(r.setPortEnabled(1, false));
    EXPECT_NEAR(r.portState(2)->outputPowerDbm, -60.0, 1e-9);
}

TEST(Roadm, TickDriftIsBoundedAndDeterministic) {
    RoadmDevice a(2, 6.0, -28.0, 99);
    RoadmDevice b(2, 6.0, -28.0, 99);
    for (int i = 0; i < 100; ++i) {
        a.tick();
        b.tick();
    }
    EXPECT_DOUBLE_EQ(a.portState(1)->inputPowerDbm,
                     b.portState(1)->inputPowerDbm);
    // 100 ticks of <=0.05 dB drift stays within 5 dB of the -10 dBm default.
    EXPECT_NEAR(a.portState(1)->inputPowerDbm, -10.0, 5.0);
}

}  // namespace
