#include <gtest/gtest.h>
#include "ipc_bridge.h"

using namespace tbox::someip;

// 模拟 PROV 客户端（通用注册/查找测试用）
class MockProvClient : public IProvClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    std::string getConfiguration(const std::string& key) override { return ""; }
};

class IpcBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
    }
    
    void TearDown() override {
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
};

TEST_F(IpcBridgeTest, InitializeSuccess) {
    // 测试初始化
    EXPECT_TRUE(bridge->initialize());
}

TEST_F(IpcBridgeTest, InitializeFailure) {
    // 测试重复初始化
    EXPECT_TRUE(bridge->initialize());
    EXPECT_FALSE(bridge->initialize());
}

TEST_F(IpcBridgeTest, RegisterClient) {
    // 测试注册客户端
    EXPECT_TRUE(bridge->initialize());
    
    // 创建模拟客户端
    auto mock_client = std::make_shared<MockProvClient>();
    EXPECT_TRUE(bridge->registerClient("prov", mock_client));
}

TEST_F(IpcBridgeTest, GetClient) {
    // 测试获取客户端
    EXPECT_TRUE(bridge->initialize());
    
    auto mock_client = std::make_shared<MockProvClient>();
    bridge->registerClient("prov", mock_client);
    
    auto retrieved = bridge->getClient("prov");
    EXPECT_EQ(retrieved, mock_client);
}

TEST_F(IpcBridgeTest, GetNonexistentClient) {
    // 测试获取不存在的客户端
    EXPECT_TRUE(bridge->initialize());
    
    auto retrieved = bridge->getClient("nonexistent");
    EXPECT_EQ(retrieved, nullptr);
}
