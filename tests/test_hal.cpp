#include "onsim/hal.h"

#include <gtest/gtest.h>

namespace {

class HalTest : public ::testing::Test {
protected:
    void SetUp() override {
        dev_ = onsim_create(4, 1234);
        ASSERT_NE(dev_, nullptr);
    }
    void TearDown() override { onsim_destroy(dev_); }
    onsim_device* dev_ = nullptr;
};

TEST_F(HalTest, RejectsTinyDeviceAndNullArgs) {
    EXPECT_EQ(onsim_create(1, 1), nullptr);
    EXPECT_EQ(onsim_xc_add(nullptr, "x", 1, 2, 3), ONSIM_ERR_INVALID_ARG);
    EXPECT_EQ(onsim_xc_add(dev_, nullptr, 1, 2, 3), ONSIM_ERR_INVALID_ARG);
    EXPECT_EQ(onsim_port_get(dev_, 1, nullptr), ONSIM_ERR_INVALID_ARG);
}

TEST_F(HalTest, CrossConnectRoundTrip) {
    EXPECT_EQ(onsim_port_set_input_power(dev_, 1, -9.0), ONSIM_OK);
    EXPECT_EQ(onsim_xc_add(dev_, "xc1", 1, 2, 33), ONSIM_OK);
    onsim_port_state st{};
    ASSERT_EQ(onsim_port_get(dev_, 2, &st), ONSIM_OK);
    EXPECT_NEAR(st.output_power_dbm, -15.0, 1e-9);
    EXPECT_EQ(st.los_alarm, 0);
    EXPECT_EQ(onsim_xc_delete(dev_, "xc1"), ONSIM_OK);
    EXPECT_EQ(onsim_xc_delete(dev_, "xc1"), ONSIM_ERR_NOT_FOUND);
}

TEST_F(HalTest, CollisionMapsToDedicatedStatus) {
    EXPECT_EQ(onsim_xc_add(dev_, "a", 1, 3, 20), ONSIM_OK);
    EXPECT_EQ(onsim_xc_add(dev_, "b", 2, 3, 20), ONSIM_ERR_COLLISION);
    EXPECT_STRNE(onsim_last_error(dev_), "");
}

TEST_F(HalTest, TransponderBusyWhileAdminUp) {
    EXPECT_EQ(onsim_xpdr_set_admin(dev_, 1), ONSIM_OK);
    EXPECT_EQ(onsim_xpdr_set_rate(dev_, 400), ONSIM_ERR_BUSY);
    EXPECT_EQ(onsim_xpdr_set_admin(dev_, 0), ONSIM_OK);
    EXPECT_EQ(onsim_xpdr_set_rate(dev_, 400), ONSIM_OK);
    EXPECT_EQ(onsim_xpdr_set_rate(dev_, 123), ONSIM_ERR_INVALID_ARG);
}

TEST_F(HalTest, StateReflectsProvisioningAndTicks) {
    ASSERT_EQ(onsim_xpdr_set_rate(dev_, 200), ONSIM_OK);
    ASSERT_EQ(onsim_xpdr_set_modulation(dev_, 1), ONSIM_OK);
    ASSERT_EQ(onsim_xpdr_set_admin(dev_, 1), ONSIM_OK);
    ASSERT_EQ(onsim_xpdr_set_osnr(dev_, 25.0), ONSIM_OK);
    onsim_xpdr_state xs{};
    ASSERT_EQ(onsim_xpdr_get(dev_, &xs), ONSIM_OK);
    EXPECT_EQ(xs.admin_up, 1);
    EXPECT_EQ(xs.line_rate_gbps, 200);
    EXPECT_EQ(xs.modulation, 1);
    EXPECT_GT(xs.pre_fec_ber, 0.0);
    onsim_tick(dev_);
    onsim_xpdr_state xs2{};
    ASSERT_EQ(onsim_xpdr_get(dev_, &xs2), ONSIM_OK);
    EXPECT_NE(xs2.osnr_db, xs.osnr_db);
}

}  // namespace
