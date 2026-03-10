#include <gtest/gtest.h>
#include "uz_input.h"

class InputTest : public ::testing::Test {
protected:
    uz::InputSystem input;
    void SetUp() override { input.Init(); }
    void TearDown() override { input.Shutdown(); }
};

TEST_F(InputTest, KeyboardPressHoldRelease) {
    // Frame 1: press W
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(uz::Key::W));
    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustPressed(uz::Key::W));
    EXPECT_FALSE(input.IsKeyDown(uz::Key::A));

    // Frame 2: W still held
    input.BeginFrame();
    EXPECT_TRUE(input.IsKeyDown(uz::Key::W));
    EXPECT_FALSE(input.IsKeyJustPressed(uz::Key::W));

    // Frame 3: release W
    input.OnKeyUp(static_cast<uint16_t>(uz::Key::W));
    EXPECT_FALSE(input.IsKeyDown(uz::Key::W));
    EXPECT_TRUE(input.IsKeyJustReleased(uz::Key::W));
}

TEST_F(InputTest, MouseMoveButtonScroll) {
    input.BeginFrame();
    input.OnMouseMove(100.f, 200.f);
    EXPECT_FLOAT_EQ(input.GetMouseX(), 100.f);
    EXPECT_FLOAT_EQ(input.GetMouseY(), 200.f);

    input.OnMouseButton(uz::MouseButton::Left, true);
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseJustPressed(uz::MouseButton::Left));

    input.BeginFrame();
    EXPECT_FALSE(input.IsMouseJustPressed(uz::MouseButton::Left));
    EXPECT_TRUE(input.IsMouseDown(uz::MouseButton::Left));

    input.OnMouseScroll(3.f);
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 3.f);
}

TEST_F(InputTest, ActionMappingWorks) {
    input.RegisterAction("Jump", uz::Key::Space);
    input.RegisterAction("Forward", uz::Key::W);

    input.BeginFrame();
    EXPECT_FALSE(input.IsActionDown("Jump"));

    input.OnKeyDown(static_cast<uint16_t>(uz::Key::Space));
    EXPECT_TRUE(input.IsActionDown("Jump"));
    EXPECT_TRUE(input.IsActionJustPressed("Jump"));
    EXPECT_FALSE(input.IsActionDown("NonExistent"));
}
