#include <gtest/gtest.h>
#include <memory>

#include "ship/Context.h"
#include "ship/resource/ResourceManager.h"
#include "ship/resource/ResourceManagerScope.h"

namespace {

// The harness never boots a Context, so GetRawInstance() is null by default. Each test stands up a minimal,
// uninitialized one (libultraship owns it) to exercise the guard's swap/restore for real without starting any
// subsystems. ~Context tolerates the null Window/Config this leaves behind.
// TearDown destroys it again, as the game's DeinitOTR does. Left to the static destructors, ~Context would run after
// spdlog's registry (created lazily by the first log call, so destroyed first) and log through a freed logger.
class ResourceManagerScope : public ::testing::Test {
  protected:
    void SetUp() override {
        mCtx = Ship::Context::CreateUninitializedInstance("LusTest", "lustest", "");
        ASSERT_NE(mCtx, nullptr);
    }

    void TearDown() override {
        Ship::Context::DestroyInstance();
    }

    Ship::Context* mCtx = nullptr;
};

// These tests use ResourceManagers purely as identity tokens (pointer equality),
// so we skip Init(): a default-constructed ResourceManager is a valid, destructible
// object (ctor is empty, ~ResourceManager only logs) and never needs its subsystems.
std::shared_ptr<Ship::ResourceManager> MakeRm() {
    return std::make_shared<Ship::ResourceManager>();
}

} // namespace

// Verifies the active RM is swapped within scope and restored after.
TEST_F(ResourceManagerScope, SwapsAndRestores) {
    auto original = MakeRm();
    mCtx->SetResourceManager(original);
    ASSERT_EQ(mCtx->GetResourceManager(), original);

    auto other = MakeRm();
    {
        Ship::ResourceManagerScope scope(other);
        EXPECT_EQ(mCtx->GetResourceManager(), other);
    }
    EXPECT_EQ(mCtx->GetResourceManager(), original);
}

// Constructing the guard with a null target must be a no-op (no swap, no crash).
TEST_F(ResourceManagerScope, NullTargetIsNoOp) {
    auto original = MakeRm();
    mCtx->SetResourceManager(original);

    {
        Ship::ResourceManagerScope scope(nullptr);
        EXPECT_EQ(mCtx->GetResourceManager(), original);
    }
    EXPECT_EQ(mCtx->GetResourceManager(), original);
}
