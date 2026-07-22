#include <gtest/gtest.h>
#include "service_discovery.h"

using namespace tbox::someip;

class ServiceDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        sd = std::make_unique<ServiceDiscovery>();
    }
    
    void TearDown() override {
        if (sd) {
            sd->shutdown();
        }
    }
    
    std::unique_ptr<ServiceDiscovery> sd;
};

TEST_F(ServiceDiscoveryTest, InitializeSuccess) {
    EXPECT_TRUE(sd->initialize());
}

TEST_F(ServiceDiscoveryTest, OfferServiceSuccess) {
    EXPECT_TRUE(sd->initialize());
    EXPECT_TRUE(sd->offerService(service_ids::TIME_SYNC, instance_ids::DEFAULT));
}

TEST_F(ServiceDiscoveryTest, OfferServiceBeforeInit) {
    EXPECT_FALSE(sd->offerService(service_ids::TIME_SYNC, instance_ids::DEFAULT));
}

TEST_F(ServiceDiscoveryTest, StopOfferService) {
    EXPECT_TRUE(sd->initialize());
    EXPECT_TRUE(sd->offerService(service_ids::TIME_SYNC, instance_ids::DEFAULT));
    
    // 停止发布不应崩溃
    sd->stopOfferService(service_ids::TIME_SYNC, instance_ids::DEFAULT);
}

TEST_F(ServiceDiscoveryTest, SubscribeServiceAvailability) {
    EXPECT_TRUE(sd->initialize());
    
    bool available = false;
    sd->subscribeServiceAvailability(
        service_ids::TIME_SYNC,
        instance_ids::DEFAULT,
        [&available](bool avail) { available = avail; }
    );
    
    // 订阅不应崩溃
}
