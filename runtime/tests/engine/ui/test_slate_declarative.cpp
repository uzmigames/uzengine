/// Unit tests for the declarative DSL (Phase 18 / Step 8).

#include "engine/ui/slate_declarative.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace uz;
using namespace uz::ui;

TEST(SlateDeclarative, NewReturnsBuilderWithConstructedWidget) {
    auto b = New<TextBlock>();
    SP<TextBlock> tb = b;
    ASSERT_NE(tb, nullptr);
}

TEST(SlateDeclarative, ChainedTextBlockSetters) {
    auto t = New<TextBlock>().Text("hello").FontSize(13.0f);
    SP<TextBlock> tb = t;
    EXPECT_EQ(tb->Text.Get(), "hello");
    EXPECT_FLOAT_EQ(tb->FontSize.Get(), 13.0f);
}

TEST(SlateDeclarative, LambdaPropAssignment) {
    int counter = 0;
    auto t = New<TextBlock>()
                 .Text(std::function<std::string()>([&counter] {
                     ++counter;
                     return std::string{"v" + std::to_string(counter)};
                 }));
    SP<TextBlock> tb = t;
    EXPECT_EQ(tb->Text.Get(), "v1");
    EXPECT_EQ(tb->Text.Get(), "v2");
}

TEST(SlateDeclarative, OperatorIndexAddsChildrenToContainer) {
    auto a = New<TextBlock>().Text("a");
    auto b = New<TextBlock>().Text("b");
    auto c = New<TextBlock>().Text("c");

    auto box = New<HorizontalBox>()[{a, b, c}];
    SP<HorizontalBox> ptr = box;
    ASSERT_EQ(ptr->SlotCount(), 3u);
    EXPECT_EQ(ptr->Slots()[0].widget, static_cast<SP<Widget>>(a));
    EXPECT_EQ(ptr->Slots()[1].widget, static_cast<SP<Widget>>(b));
    EXPECT_EQ(ptr->Slots()[2].widget, static_cast<SP<Widget>>(c));
}

TEST(SlateDeclarative, NewHBoxAndVBoxAlias) {
    auto h = NewHBox();
    auto v = NewVBox();
    SP<HorizontalBox> hp = h;
    SP<VerticalBox>   vp = v;
    EXPECT_NE(hp, nullptr);
    EXPECT_NE(vp, nullptr);
}

TEST(SlateDeclarative, ButtonContentViaIndex) {
    auto label = New<TextBlock>().Text("Click");
    auto btn = New<Button>()[{label}];
    SP<Button> ptr = btn;
    ASSERT_NE(ptr->Content, nullptr);
    auto labelPtr = std::dynamic_pointer_cast<TextBlock>(ptr->Content);
    ASSERT_NE(labelPtr, nullptr);
    EXPECT_EQ(labelPtr->Text.Get(), "Click");
}

TEST(SlateDeclarative, ButtonOnClickSetter) {
    int clicks = 0;
    auto btn = New<Button>().OnClick([&clicks] { ++clicks; });
    SP<Button> ptr = btn;
    ASSERT_TRUE(ptr->OnClick);
    ptr->OnClick();
    EXPECT_EQ(clicks, 1);
}

TEST(SlateDeclarative, SlotHelperWrapsWidgetWithDefaultSpec) {
    auto t = New<TextBlock>().Text("x");
    auto sb = Slot(t);
    EXPECT_EQ(sb.spec.sizeRule, SizeRule::Auto);
    EXPECT_EQ(sb.spec.hAlign, HAlign::Fill);
}

TEST(SlateDeclarative, SlotHelperFluentSetters) {
    auto t = New<TextBlock>().Text("x");
    auto sb = Slot(t).FillSize(2.0f).HAlign(HAlign::Right).Padding(4.0f);
    EXPECT_EQ(sb.spec.sizeRule, SizeRule::Fill);
    EXPECT_FLOAT_EQ(sb.spec.fillFactor, 2.0f);
    EXPECT_EQ(sb.spec.hAlign, HAlign::Right);
    EXPECT_FLOAT_EQ(sb.spec.padding.x, 4.0f);
}

TEST(SlateDeclarative, ImplicitConversionToBaseWidget) {
    auto t = New<TextBlock>().Text("x");
    SP<Widget> w = t;
    EXPECT_NE(w, nullptr);
}

TEST(SlateDeclarative, NestedTreeBuildsCorrectStructure) {
    auto root = NewVBox()
        [{
            New<TextBlock>().Text("Header"),
            NewHBox()
                [{
                    New<Button>().Padding(8.0f),
                    NewSpacer(),
                    New<TextBlock>().Text("Right"),
                }],
        }];

    SP<VerticalBox> vroot = root;
    ASSERT_EQ(vroot->SlotCount(), 2u);
    auto hbox = std::dynamic_pointer_cast<HorizontalBox>(vroot->Slots()[1].widget);
    ASSERT_NE(hbox, nullptr);
    EXPECT_EQ(hbox->SlotCount(), 3u);
}
