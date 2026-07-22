#include <gtest/gtest.h>
#include "someip_daemon.h"

using namespace tbox::someip;

class SomeipDaemonTest : public ::testing::Test {
protected:
    void SetUp() override {
        daemon = std::make_unique<SomeipDaemon>();
    }
    
    void TearDown() override {
        if (daemon) {
            daemon->shutdown();
        }
    }
    
    std::unique_ptr<SomeipDaemon> daemon;
};

TEST_F(SomeipDaemonTest, InitializeSuccess) {
    EXPECT_TRUE(daemon->initialize());
}

TEST_F(SomeipDaemonTest, StartWithoutInit) {
    EXPECT_FALSE(daemon->start());
}

TEST_F(SomeipDaemonTest, Shutdown) {
    EXPECT_TRUE(daemon->initialize());
    EXPECT_TRUE(daemon->start());
    
    // 关闭不应崩溃
    daemon->shutdown();
}
