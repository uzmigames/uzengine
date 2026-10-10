#include <gtest/gtest.h>
#include "../../../tools/capture_sequence.h"

TEST(CaptureSequence, ParsesSingleRangeAndStep) {
    const auto single = uz::CaptureSequence::Parse("0");
    ASSERT_TRUE(single);
    EXPECT_EQ(single->first, 0u);
    EXPECT_EQ(single->last, 0u);
    const auto range = uz::CaptureSequence::Parse("1-9:2");
    ASSERT_TRUE(range);
    EXPECT_TRUE(range->Includes(1, true, false, false));
    EXPECT_TRUE(range->Includes(9, true, false, false));
    EXPECT_FALSE(range->Includes(2, true, false, false));
    EXPECT_FALSE(range->Includes(0, true, false, false));
    EXPECT_FALSE(range->Includes(10, true, false, false));
    const auto maximum = uz::CaptureSequence::Parse("4294967295");
    ASSERT_TRUE(maximum);
    EXPECT_TRUE(maximum->Includes(UINT32_MAX, true, false, false));
}

TEST(CaptureSequence, RejectsMalformedOrOverflowingRanges) {
    for (const auto* value : {"", "-1", "+1", "1-", "9-1", "1-9:0", "1-9:",
                             "1-9:2junk", "1garbage", "1:2", "4294967296", " 1", "1 "}) {
        EXPECT_FALSE(uz::CaptureSequence::Parse(value)) << value;
    }
}

TEST(CaptureSequence, PreloadCaptureRequiresExplicitOptIn) {
    const auto range = uz::CaptureSequence::Parse("0-8");
    ASSERT_TRUE(range);
    EXPECT_FALSE(range->Includes(0, false, true, false));
    EXPECT_FALSE(range->Includes(2, true, true, false));
    EXPECT_FALSE(range->Includes(4, false, false, false));
    EXPECT_TRUE(range->Includes(0, false, true, true));
    EXPECT_TRUE(range->Includes(2, true, true, true));
    EXPECT_TRUE(range->Includes(4, true, false, false));
}

TEST(CaptureSequence, NamesFramesWithOrWithoutDirectoryAndExtension) {
    EXPECT_EQ(uz::CaptureSequence::ImagePath("frame.png", 0), "frame_0.png");
    EXPECT_EQ(uz::CaptureSequence::ImagePath("frame", 2), "frame_2.png");
    EXPECT_EQ(std::filesystem::path(uz::CaptureSequence::ImagePath("images.v1/frame.png", 4)),
              std::filesystem::path("images.v1/frame_4.png"));
}
