/// Unit tests for the UzEngine Property System.
///
/// Covers: PropertyType, PropertyFlags, PropertyInfo, ClassInfo, ClassRegistry,
/// UZ_COMPONENT macro, GetPropertyType<T> specializations, PropertySerializer
/// (JSON serialize/deserialize, transient flag exclusion, missing/unknown fields).
///
/// No GPU, RHI, or audio hardware required.
/// Coverage targets (Phase 3b-3.2b-property-system):
///   - property_types.h      → 100%
///   - property_info.h       → 100%
///   - property_macros.h     → 100%
///   - class_info.h/.cpp     → 100%
///   - class_registry.h/.cpp → 100%
///   - property_serializer.h/.cpp → 95%+

#include "property_macros.h"
#include "property_serializer.h"
#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "quaternion.h"
#include "entity.h"

#include <gtest/gtest.h>

#include <string>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Test components — defined locally, never in engine headers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Full-coverage test component with one field of every supported PropertyType.
struct AllTypesComponent {
    UZ_COMPONENT(AllTypesComponent);

    bool   flag      = false;
    int    count     = 42;
    float  speed     = 3.14f;
    double precision = 2.718281828;
    std::string label = "default";
    Vec2   uv{0.1f, 0.2f};
    Vec3   position{1.0f, 2.0f, 3.0f};
    Vec4   color{1.0f, 0.0f, 0.0f, 1.0f};
    Quat   rotation  = Quat::Identity();
    Entity target    = Entity::Invalid();

    UZ_PROPERTIES_BEGIN(AllTypesComponent)
        UZ_PROP(bool,        flag,      "A boolean flag")
        UZ_PROP(int,         count,     "Item count")
        UZ_PROP(float,       speed,     "Movement speed")
        UZ_PROP(double,      precision, "Precision value")
        UZ_PROP(std::string, label,     "Display label")
        UZ_PROP(uz::Vec2,   uv,        "Texture UV")
        UZ_PROP(uz::Vec3,   position,  "World position")
        UZ_PROP(uz::Vec4,   color,     "RGBA color")
        UZ_PROP(uz::Quat,   rotation,  "Orientation")
        UZ_PROP(uz::Entity, target,    "Target entity")
    UZ_PROPERTIES_END()
};

/// Component that mixes a visible property with a Transient-flagged one.
struct TransientComponent {
    UZ_COMPONENT(TransientComponent);

    float visible = 1.0f;
    int   hidden  = 99;

    static void RegisterProperties(ClassInfo& info) {
        info.AddProperty<float>("visible", offsetof(TransientComponent, visible),
                                "Serialized property");
        info.AddProperty<int>("hidden", offsetof(TransientComponent, hidden),
                              "Skipped by serializer", "",
                              PropertyFlags::Transient);
    }
};

/// Minimal component used to verify re-registration and ClassCount behaviour.
struct MinimalComponent {
    UZ_COMPONENT(MinimalComponent);

    int value = 0;

    static void RegisterProperties(ClassInfo& info) {
        info.AddProperty<int>("value", offsetof(MinimalComponent, value), "A value");
    }
};

/// Component that exercises AddProperty with category and multiple flags.
struct FlaggedComponent {
    UZ_COMPONENT(FlaggedComponent);

    float readonlyField = 5.0f;
    int   hiddenField   = 7;

    static void RegisterProperties(ClassInfo& info) {
        info.AddProperty<float>("readonlyField", offsetof(FlaggedComponent, readonlyField),
                                "Read-only property", "Debug",
                                PropertyFlags::ReadOnly);
        info.AddProperty<int>("hiddenField", offsetof(FlaggedComponent, hiddenField),
                              "Hidden property", "Debug",
                              PropertyFlags::Hidden | PropertyFlags::Transient);
    }
};

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Section 1 — PropertyType & PropertyTypeName
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertyType, TypeName_None_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::None), "None");
}

TEST(PropertyType, TypeName_Bool_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Bool), "Bool");
}

TEST(PropertyType, TypeName_Int_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Int), "Int");
}

TEST(PropertyType, TypeName_Float_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Float), "Float");
}

TEST(PropertyType, TypeName_Double_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Double), "Double");
}

TEST(PropertyType, TypeName_String_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::String), "String");
}

TEST(PropertyType, TypeName_Vec2_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Vec2), "Vec2");
}

TEST(PropertyType, TypeName_Vec3_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Vec3), "Vec3");
}

TEST(PropertyType, TypeName_Vec4_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Vec4), "Vec4");
}

TEST(PropertyType, TypeName_Quat_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Quat), "Quat");
}

TEST(PropertyType, TypeName_Entity_ReturnsCorrectString) {
    EXPECT_STREQ(PropertyTypeName(PropertyType::Entity), "Entity");
}

TEST(PropertyType, TypeName_UnknownValue_ReturnsUnknown) {
    // Cast an out-of-range value to exercise the default branch.
    PropertyType unknown = static_cast<PropertyType>(0xFF);
    EXPECT_STREQ(PropertyTypeName(unknown), "Unknown");
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 2 — PropertyFlags bitwise operators and HasFlag
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertyFlags, Or_CombinesTwoFlags) {
    PropertyFlags combined = PropertyFlags::ReadOnly | PropertyFlags::Hidden;
    // Both bits must be set
    EXPECT_TRUE(HasFlag(combined, PropertyFlags::ReadOnly));
    EXPECT_TRUE(HasFlag(combined, PropertyFlags::Hidden));
    EXPECT_FALSE(HasFlag(combined, PropertyFlags::Replicated));
}

TEST(PropertyFlags, And_IsolatesFlag) {
    PropertyFlags combined = PropertyFlags::ReadOnly | PropertyFlags::Transient;
    PropertyFlags masked   = combined & PropertyFlags::ReadOnly;
    EXPECT_TRUE(HasFlag(masked, PropertyFlags::ReadOnly));
    EXPECT_FALSE(HasFlag(masked, PropertyFlags::Transient));
}

TEST(PropertyFlags, Xor_TogglesFlag) {
    PropertyFlags flags   = PropertyFlags::ReadOnly | PropertyFlags::Hidden;
    PropertyFlags toggled = flags ^ PropertyFlags::ReadOnly;
    EXPECT_FALSE(HasFlag(toggled, PropertyFlags::ReadOnly));
    EXPECT_TRUE(HasFlag(toggled, PropertyFlags::Hidden));
}

TEST(PropertyFlags, BitwiseNot_InvertsAllBits) {
    PropertyFlags none    = PropertyFlags::None;
    PropertyFlags inverted = ~none;
    // Every single-bit flag should now be set.
    EXPECT_TRUE(HasFlag(inverted, PropertyFlags::ReadOnly));
    EXPECT_TRUE(HasFlag(inverted, PropertyFlags::Hidden));
    EXPECT_TRUE(HasFlag(inverted, PropertyFlags::Replicated));
    EXPECT_TRUE(HasFlag(inverted, PropertyFlags::SaveGame));
    EXPECT_TRUE(HasFlag(inverted, PropertyFlags::Transient));
}

TEST(PropertyFlags, CompoundOrAssign_UpdatesInPlace) {
    PropertyFlags flags = PropertyFlags::None;
    flags |= PropertyFlags::SaveGame;
    EXPECT_TRUE(HasFlag(flags, PropertyFlags::SaveGame));
    EXPECT_FALSE(HasFlag(flags, PropertyFlags::Hidden));
}

TEST(PropertyFlags, CompoundAndAssign_UpdatesInPlace) {
    PropertyFlags flags = PropertyFlags::ReadOnly | PropertyFlags::Transient;
    flags &= PropertyFlags::Transient;
    EXPECT_FALSE(HasFlag(flags, PropertyFlags::ReadOnly));
    EXPECT_TRUE(HasFlag(flags, PropertyFlags::Transient));
}

TEST(PropertyFlags, HasFlag_None_ReturnsFalseForAnyFlag) {
    EXPECT_FALSE(HasFlag(PropertyFlags::None, PropertyFlags::ReadOnly));
    EXPECT_FALSE(HasFlag(PropertyFlags::None, PropertyFlags::Transient));
}

TEST(PropertyFlags, HasFlag_MultipleFlagsSet_ReturnsTrueForEach) {
    PropertyFlags flags = PropertyFlags::ReadOnly | PropertyFlags::Replicated | PropertyFlags::SaveGame;
    EXPECT_TRUE(HasFlag(flags, PropertyFlags::ReadOnly));
    EXPECT_TRUE(HasFlag(flags, PropertyFlags::Replicated));
    EXPECT_TRUE(HasFlag(flags, PropertyFlags::SaveGame));
    EXPECT_FALSE(HasFlag(flags, PropertyFlags::Hidden));
    EXPECT_FALSE(HasFlag(flags, PropertyFlags::Transient));
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 3 — GetPropertyType<T> specializations
// ─────────────────────────────────────────────────────────────────────────────

TEST(GetPropertyType, Bool_ReturnsBool) {
    EXPECT_EQ(GetPropertyType<bool>(), PropertyType::Bool);
}

TEST(GetPropertyType, Int_ReturnsInt) {
    EXPECT_EQ(GetPropertyType<int>(), PropertyType::Int);
}

TEST(GetPropertyType, Float_ReturnsFloat) {
    EXPECT_EQ(GetPropertyType<float>(), PropertyType::Float);
}

TEST(GetPropertyType, Double_ReturnsDouble) {
    EXPECT_EQ(GetPropertyType<double>(), PropertyType::Double);
}

TEST(GetPropertyType, StdString_ReturnsString) {
    EXPECT_EQ(GetPropertyType<std::string>(), PropertyType::String);
}

TEST(GetPropertyType, Vec2_ReturnsVec2) {
    EXPECT_EQ(GetPropertyType<Vec2>(), PropertyType::Vec2);
}

TEST(GetPropertyType, Vec3_ReturnsVec3) {
    EXPECT_EQ(GetPropertyType<Vec3>(), PropertyType::Vec3);
}

TEST(GetPropertyType, Vec4_ReturnsVec4) {
    EXPECT_EQ(GetPropertyType<Vec4>(), PropertyType::Vec4);
}

TEST(GetPropertyType, Quat_ReturnsQuat) {
    EXPECT_EQ(GetPropertyType<Quat>(), PropertyType::Quat);
}

TEST(GetPropertyType, Entity_ReturnsEntity) {
    EXPECT_EQ(GetPropertyType<Entity>(), PropertyType::Entity);
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 4 — ClassInfo: AddProperty, FindProperty, ForEachProperty, PropertyCount
// ─────────────────────────────────────────────────────────────────────────────

TEST(ClassInfo, AddProperty_PopulatesCorrectType) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    const PropertyInfo* flag_prop = info.FindProperty("flag");
    ASSERT_NE(flag_prop, nullptr);
    EXPECT_EQ(flag_prop->type, PropertyType::Bool);

    const PropertyInfo* count_prop = info.FindProperty("count");
    ASSERT_NE(count_prop, nullptr);
    EXPECT_EQ(count_prop->type, PropertyType::Int);

    const PropertyInfo* speed_prop = info.FindProperty("speed");
    ASSERT_NE(speed_prop, nullptr);
    EXPECT_EQ(speed_prop->type, PropertyType::Float);

    const PropertyInfo* precision_prop = info.FindProperty("precision");
    ASSERT_NE(precision_prop, nullptr);
    EXPECT_EQ(precision_prop->type, PropertyType::Double);

    const PropertyInfo* label_prop = info.FindProperty("label");
    ASSERT_NE(label_prop, nullptr);
    EXPECT_EQ(label_prop->type, PropertyType::String);

    const PropertyInfo* uv_prop = info.FindProperty("uv");
    ASSERT_NE(uv_prop, nullptr);
    EXPECT_EQ(uv_prop->type, PropertyType::Vec2);

    const PropertyInfo* position_prop = info.FindProperty("position");
    ASSERT_NE(position_prop, nullptr);
    EXPECT_EQ(position_prop->type, PropertyType::Vec3);

    const PropertyInfo* color_prop = info.FindProperty("color");
    ASSERT_NE(color_prop, nullptr);
    EXPECT_EQ(color_prop->type, PropertyType::Vec4);

    const PropertyInfo* rotation_prop = info.FindProperty("rotation");
    ASSERT_NE(rotation_prop, nullptr);
    EXPECT_EQ(rotation_prop->type, PropertyType::Quat);

    const PropertyInfo* target_prop = info.FindProperty("target");
    ASSERT_NE(target_prop, nullptr);
    EXPECT_EQ(target_prop->type, PropertyType::Entity);
}

TEST(ClassInfo, AddProperty_PopulatesCorrectOffset) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    const PropertyInfo* flag_prop = info.FindProperty("flag");
    ASSERT_NE(flag_prop, nullptr);
    EXPECT_EQ(flag_prop->offset, offsetof(AllTypesComponent, flag));

    const PropertyInfo* count_prop = info.FindProperty("count");
    ASSERT_NE(count_prop, nullptr);
    EXPECT_EQ(count_prop->offset, offsetof(AllTypesComponent, count));

    const PropertyInfo* speed_prop = info.FindProperty("speed");
    ASSERT_NE(speed_prop, nullptr);
    EXPECT_EQ(speed_prop->offset, offsetof(AllTypesComponent, speed));

    const PropertyInfo* precision_prop = info.FindProperty("precision");
    ASSERT_NE(precision_prop, nullptr);
    EXPECT_EQ(precision_prop->offset, offsetof(AllTypesComponent, precision));

    const PropertyInfo* label_prop = info.FindProperty("label");
    ASSERT_NE(label_prop, nullptr);
    EXPECT_EQ(label_prop->offset, offsetof(AllTypesComponent, label));

    const PropertyInfo* uv_prop = info.FindProperty("uv");
    ASSERT_NE(uv_prop, nullptr);
    EXPECT_EQ(uv_prop->offset, offsetof(AllTypesComponent, uv));

    const PropertyInfo* position_prop = info.FindProperty("position");
    ASSERT_NE(position_prop, nullptr);
    EXPECT_EQ(position_prop->offset, offsetof(AllTypesComponent, position));

    const PropertyInfo* color_prop = info.FindProperty("color");
    ASSERT_NE(color_prop, nullptr);
    EXPECT_EQ(color_prop->offset, offsetof(AllTypesComponent, color));

    const PropertyInfo* rotation_prop = info.FindProperty("rotation");
    ASSERT_NE(rotation_prop, nullptr);
    EXPECT_EQ(rotation_prop->offset, offsetof(AllTypesComponent, rotation));

    const PropertyInfo* target_prop = info.FindProperty("target");
    ASSERT_NE(target_prop, nullptr);
    EXPECT_EQ(target_prop->offset, offsetof(AllTypesComponent, target));
}

TEST(ClassInfo, AddProperty_PopulatesCorrectSize) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    ASSERT_NE(info.FindProperty("flag"),      nullptr);
    ASSERT_NE(info.FindProperty("count"),     nullptr);
    ASSERT_NE(info.FindProperty("speed"),     nullptr);
    ASSERT_NE(info.FindProperty("precision"), nullptr);
    ASSERT_NE(info.FindProperty("label"),     nullptr);
    ASSERT_NE(info.FindProperty("uv"),        nullptr);
    ASSERT_NE(info.FindProperty("position"),  nullptr);
    ASSERT_NE(info.FindProperty("color"),     nullptr);
    ASSERT_NE(info.FindProperty("rotation"),  nullptr);
    ASSERT_NE(info.FindProperty("target"),    nullptr);

    EXPECT_EQ(info.FindProperty("flag")->size,      sizeof(bool));
    EXPECT_EQ(info.FindProperty("count")->size,     sizeof(int));
    EXPECT_EQ(info.FindProperty("speed")->size,     sizeof(float));
    EXPECT_EQ(info.FindProperty("precision")->size, sizeof(double));
    EXPECT_EQ(info.FindProperty("label")->size,     sizeof(std::string));
    EXPECT_EQ(info.FindProperty("uv")->size,        sizeof(Vec2));
    EXPECT_EQ(info.FindProperty("position")->size,  sizeof(Vec3));
    EXPECT_EQ(info.FindProperty("color")->size,     sizeof(Vec4));
    EXPECT_EQ(info.FindProperty("rotation")->size,  sizeof(Quat));
    EXPECT_EQ(info.FindProperty("target")->size,    sizeof(Entity));
}

TEST(ClassInfo, FindProperty_NonExistentName_ReturnsNullptr) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    EXPECT_EQ(info.FindProperty("nonexistent"), nullptr);
    EXPECT_EQ(info.FindProperty(""),            nullptr);
}

TEST(ClassInfo, PropertyCount_MatchesRegisteredProperties) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    // AllTypesComponent registers 10 properties (bool,int,float,double,string,vec2,vec3,vec4,quat,entity)
    EXPECT_EQ(info.PropertyCount(), 10u);
}

TEST(ClassInfo, PropertyCount_MinimalComponent_IsOne) {
    const ClassInfo& info = MinimalComponent::GetStaticClassInfo();
    EXPECT_EQ(info.PropertyCount(), 1u);
}

TEST(ClassInfo, ForEachProperty_VisitsAllProperties) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    size_t visit_count = 0;
    info.ForEachProperty([&](const PropertyInfo&) { ++visit_count; });
    EXPECT_EQ(visit_count, info.PropertyCount());
}

TEST(ClassInfo, ForEachProperty_EmptyComponent_NeverCallsCallback) {
    // Build a local ClassInfo with no properties.
    ClassInfo empty_info;
    empty_info.name = "EmptyTest";
    empty_info.instance_size = 1;

    bool called = false;
    empty_info.ForEachProperty([&](const PropertyInfo&) { called = true; });
    EXPECT_FALSE(called);
}

TEST(ClassInfo, FindProperty_CaseSensitive_DifferentCaseReturnsNullptr) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    // "flag" exists; "Flag" and "FLAG" must not match.
    EXPECT_NE(info.FindProperty("flag"), nullptr);
    EXPECT_EQ(info.FindProperty("Flag"), nullptr);
    EXPECT_EQ(info.FindProperty("FLAG"), nullptr);
}

TEST(ClassInfo, AddProperty_Category_IsStoredCorrectly) {
    const ClassInfo& info = FlaggedComponent::GetStaticClassInfo();
    const PropertyInfo* ro = info.FindProperty("readonlyField");
    ASSERT_NE(ro, nullptr);
    EXPECT_STREQ(ro->category, "Debug");
}

TEST(ClassInfo, AddProperty_Flags_AreStoredCorrectly) {
    const ClassInfo& info = FlaggedComponent::GetStaticClassInfo();

    const PropertyInfo* ro = info.FindProperty("readonlyField");
    ASSERT_NE(ro, nullptr);
    EXPECT_TRUE(HasFlag(ro->flags, PropertyFlags::ReadOnly));
    EXPECT_FALSE(HasFlag(ro->flags, PropertyFlags::Hidden));

    const PropertyInfo* hidden = info.FindProperty("hiddenField");
    ASSERT_NE(hidden, nullptr);
    EXPECT_TRUE(HasFlag(hidden->flags, PropertyFlags::Hidden));
    EXPECT_TRUE(HasFlag(hidden->flags, PropertyFlags::Transient));
    EXPECT_FALSE(HasFlag(hidden->flags, PropertyFlags::ReadOnly));
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 5 — UZ_COMPONENT macro: auto-registration, GetStaticClassInfo
// ─────────────────────────────────────────────────────────────────────────────

TEST(UzComponent, GetStaticClassInfo_ReturnsCorrectName) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    EXPECT_EQ(info.name, "AllTypesComponent");
}

TEST(UzComponent, GetStaticClassInfo_ReturnsCorrectInstanceSize) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    EXPECT_EQ(info.instance_size, sizeof(AllTypesComponent));
}

TEST(UzComponent, GetStaticClassInfo_FactoryPlacementNewProducesDefaultInstance) {
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    ASSERT_NE(info.factory, nullptr);

    // Allocate aligned storage and construct via factory.
    alignas(AllTypesComponent) std::byte storage[sizeof(AllTypesComponent)];
    void* obj = info.factory(storage);
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj, static_cast<void*>(storage));

    auto* comp = static_cast<AllTypesComponent*>(obj);
    EXPECT_EQ(comp->count, 42)  << "Default count must be 42";
    EXPECT_FLOAT_EQ(comp->speed, 3.14f);
    comp->~AllTypesComponent();
}

TEST(UzComponent, AutoRegistration_ComponentFoundInClassRegistry) {
    const ClassInfo* info = ClassRegistry::Get().FindClass("AllTypesComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "AllTypesComponent");
}

TEST(UzComponent, AutoRegistration_TransientComponent_IsRegistered) {
    EXPECT_TRUE(ClassRegistry::Get().IsRegistered("TransientComponent"));
}

TEST(UzComponent, AutoRegistration_MinimalComponent_IsRegistered) {
    EXPECT_TRUE(ClassRegistry::Get().IsRegistered("MinimalComponent"));
}

TEST(UzComponent, AutoRegistration_FlaggedComponent_IsRegistered) {
    EXPECT_TRUE(ClassRegistry::Get().IsRegistered("FlaggedComponent"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 6 — ClassRegistry: Find, IsRegistered, ForEachClass, ClassCount
// ─────────────────────────────────────────────────────────────────────────────

TEST(ClassRegistry, FindClass_RegisteredName_ReturnsNonNull) {
    ClassInfo* info = ClassRegistry::Get().FindClass("AllTypesComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "AllTypesComponent");
}

TEST(ClassRegistry, FindClass_ConstOverload_RegisteredName_ReturnsNonNull) {
    const ClassRegistry& reg = ClassRegistry::Get();
    const ClassInfo* info = reg.FindClass("AllTypesComponent");
    ASSERT_NE(info, nullptr);
    EXPECT_EQ(info->name, "AllTypesComponent");
}

TEST(ClassRegistry, FindClass_UnregisteredName_ReturnsNullptr) {
    ClassInfo* info = ClassRegistry::Get().FindClass("NoSuchClass");
    EXPECT_EQ(info, nullptr);
}

TEST(ClassRegistry, FindClass_EmptyName_ReturnsNullptr) {
    EXPECT_EQ(ClassRegistry::Get().FindClass(""), nullptr);
}

TEST(ClassRegistry, IsRegistered_KnownClass_ReturnsTrue) {
    EXPECT_TRUE(ClassRegistry::Get().IsRegistered("AllTypesComponent"));
    EXPECT_TRUE(ClassRegistry::Get().IsRegistered("TransientComponent"));
    EXPECT_TRUE(ClassRegistry::Get().IsRegistered("MinimalComponent"));
}

TEST(ClassRegistry, IsRegistered_UnknownClass_ReturnsFalse) {
    EXPECT_FALSE(ClassRegistry::Get().IsRegistered("NotAClass"));
    EXPECT_FALSE(ClassRegistry::Get().IsRegistered(""));
}

TEST(ClassRegistry, ForEachClass_VisitsAllTestComponents) {
    bool saw_all_types   = false;
    bool saw_transient   = false;
    bool saw_minimal     = false;
    bool saw_flagged     = false;

    ClassRegistry::Get().ForEachClass(
        [&](std::string_view name, const ClassInfo&) {
            if (name == "AllTypesComponent")  saw_all_types = true;
            if (name == "TransientComponent") saw_transient = true;
            if (name == "MinimalComponent")   saw_minimal   = true;
            if (name == "FlaggedComponent")   saw_flagged   = true;
        });

    EXPECT_TRUE(saw_all_types)  << "AllTypesComponent must appear in ForEachClass";
    EXPECT_TRUE(saw_transient)  << "TransientComponent must appear in ForEachClass";
    EXPECT_TRUE(saw_minimal)    << "MinimalComponent must appear in ForEachClass";
    EXPECT_TRUE(saw_flagged)    << "FlaggedComponent must appear in ForEachClass";
}

TEST(ClassRegistry, ClassCount_IncludesAllTestComponents) {
    // At least our 4 test components are registered. Engine components may add more.
    size_t count = ClassRegistry::Get().ClassCount();
    EXPECT_GE(count, 4u);
}

TEST(ClassRegistry, Register_OverwritingSameName_DoesNotCrash) {
    // Register MinimalComponent again — should overwrite silently without crash.
    ClassInfo& info = MinimalComponent::GetStaticClassInfo();
    EXPECT_NO_THROW(ClassRegistry::Get().Register("MinimalComponent", info));
    // Still findable after re-registration.
    EXPECT_NE(ClassRegistry::Get().FindClass("MinimalComponent"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 7 — PropertySerializer: SerializeToJSON
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertySerializer, SerializeToJSON_Bool_EmitsCorrectValue) {
    AllTypesComponent comp;
    comp.flag = true;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("flag"));
    EXPECT_TRUE(json["flag"].get<bool>());
}

TEST(PropertySerializer, SerializeToJSON_Int_EmitsCorrectValue) {
    AllTypesComponent comp;
    comp.count = 77;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("count"));
    EXPECT_EQ(json["count"].get<int>(), 77);
}

TEST(PropertySerializer, SerializeToJSON_Float_EmitsCorrectValue) {
    AllTypesComponent comp;
    comp.speed = 9.81f;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("speed"));
    EXPECT_FLOAT_EQ(json["speed"].get<float>(), 9.81f);
}

TEST(PropertySerializer, SerializeToJSON_Double_EmitsCorrectValue) {
    AllTypesComponent comp;
    comp.precision = 3.141592653589793;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("precision"));
    EXPECT_DOUBLE_EQ(json["precision"].get<double>(), 3.141592653589793);
}

TEST(PropertySerializer, SerializeToJSON_String_EmitsCorrectValue) {
    AllTypesComponent comp;
    comp.label = "hero";
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("label"));
    EXPECT_EQ(json["label"].get<std::string>(), "hero");
}

TEST(PropertySerializer, SerializeToJSON_Vec2_EmitsArrayOfTwo) {
    AllTypesComponent comp;
    comp.uv = Vec2{0.25f, 0.75f};
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("uv"));
    ASSERT_TRUE(json["uv"].is_array());
    ASSERT_EQ(json["uv"].size(), 2u);
    EXPECT_FLOAT_EQ(json["uv"][0].get<float>(), 0.25f);
    EXPECT_FLOAT_EQ(json["uv"][1].get<float>(), 0.75f);
}

TEST(PropertySerializer, SerializeToJSON_Vec3_EmitsArrayOfThree) {
    AllTypesComponent comp;
    comp.position = Vec3{10.0f, 20.0f, 30.0f};
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("position"));
    ASSERT_TRUE(json["position"].is_array());
    ASSERT_EQ(json["position"].size(), 3u);
    EXPECT_FLOAT_EQ(json["position"][0].get<float>(), 10.0f);
    EXPECT_FLOAT_EQ(json["position"][1].get<float>(), 20.0f);
    EXPECT_FLOAT_EQ(json["position"][2].get<float>(), 30.0f);
}

TEST(PropertySerializer, SerializeToJSON_Vec4_EmitsArrayOfFour) {
    AllTypesComponent comp;
    comp.color = Vec4{0.1f, 0.2f, 0.3f, 0.4f};
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("color"));
    ASSERT_TRUE(json["color"].is_array());
    ASSERT_EQ(json["color"].size(), 4u);
    EXPECT_FLOAT_EQ(json["color"][0].get<float>(), 0.1f);
    EXPECT_FLOAT_EQ(json["color"][1].get<float>(), 0.2f);
    EXPECT_FLOAT_EQ(json["color"][2].get<float>(), 0.3f);
    EXPECT_FLOAT_EQ(json["color"][3].get<float>(), 0.4f);
}

TEST(PropertySerializer, SerializeToJSON_Quat_EmitsArrayOfFour) {
    AllTypesComponent comp;
    comp.rotation = Quat{0.0f, 0.0f, 0.7071f, 0.7071f};
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("rotation"));
    ASSERT_TRUE(json["rotation"].is_array());
    ASSERT_EQ(json["rotation"].size(), 4u);
    EXPECT_NEAR(json["rotation"][0].get<float>(), 0.0f,    1e-4f);
    EXPECT_NEAR(json["rotation"][1].get<float>(), 0.0f,    1e-4f);
    EXPECT_NEAR(json["rotation"][2].get<float>(), 0.7071f, 1e-4f);
    EXPECT_NEAR(json["rotation"][3].get<float>(), 0.7071f, 1e-4f);
}

TEST(PropertySerializer, SerializeToJSON_Entity_EmitsIdAndGeneration) {
    AllTypesComponent comp;
    comp.target.id         = 42u;
    comp.target.generation = 3u;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    ASSERT_TRUE(json.contains("target"));
    ASSERT_TRUE(json["target"].is_object());
    EXPECT_EQ(json["target"]["id"].get<uint32_t>(),         42u);
    EXPECT_EQ(json["target"]["generation"].get<uint32_t>(), 3u);
}

TEST(PropertySerializer, SerializeToJSON_TransientProperty_IsExcluded) {
    TransientComponent comp;
    comp.visible = 5.0f;
    comp.hidden  = 123;
    const ClassInfo& info = TransientComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    EXPECT_TRUE(json.contains("visible"))  << "'visible' must be serialized";
    EXPECT_FALSE(json.contains("hidden")) << "'hidden' (Transient) must be excluded";
}

TEST(PropertySerializer, SerializeToJSON_ResultIsJsonObject) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    auto json = PropertySerializer::SerializeToJSON(&comp, info);

    EXPECT_TRUE(json.is_object());
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 8 — PropertySerializer: DeserializeFromJSON
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertySerializer, DeserializeFromJSON_Bool_OverwritesField) {
    AllTypesComponent comp;
    comp.flag = false;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"flag", true}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(comp.flag);
}

TEST(PropertySerializer, DeserializeFromJSON_Int_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"count", 999}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_EQ(comp.count, 999);
}

TEST(PropertySerializer, DeserializeFromJSON_Float_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"speed", 42.5f}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(comp.speed, 42.5f);
}

TEST(PropertySerializer, DeserializeFromJSON_Double_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"precision", 1.23456789012345}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_DOUBLE_EQ(comp.precision, 1.23456789012345);
}

TEST(PropertySerializer, DeserializeFromJSON_String_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"label", "warrior"}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_EQ(comp.label, "warrior");
}

TEST(PropertySerializer, DeserializeFromJSON_Vec2_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"uv", nlohmann::json::array({0.3f, 0.6f})}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(comp.uv.x, 0.3f);
    EXPECT_FLOAT_EQ(comp.uv.y, 0.6f);
}

TEST(PropertySerializer, DeserializeFromJSON_Vec3_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"position", nlohmann::json::array({7.0f, 8.0f, 9.0f})}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(comp.position.x, 7.0f);
    EXPECT_FLOAT_EQ(comp.position.y, 8.0f);
    EXPECT_FLOAT_EQ(comp.position.z, 9.0f);
}

TEST(PropertySerializer, DeserializeFromJSON_Vec4_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"color", nlohmann::json::array({0.5f, 0.6f, 0.7f, 0.8f})}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(comp.color.x, 0.5f);
    EXPECT_FLOAT_EQ(comp.color.y, 0.6f);
    EXPECT_FLOAT_EQ(comp.color.z, 0.7f);
    EXPECT_FLOAT_EQ(comp.color.w, 0.8f);
}

TEST(PropertySerializer, DeserializeFromJSON_Quat_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"rotation", nlohmann::json::array({0.1f, 0.2f, 0.3f, 0.9274f})}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(comp.rotation.x, 0.1f);
    EXPECT_FLOAT_EQ(comp.rotation.y, 0.2f);
    EXPECT_FLOAT_EQ(comp.rotation.z, 0.3f);
    EXPECT_FLOAT_EQ(comp.rotation.w, 0.9274f);
}

TEST(PropertySerializer, DeserializeFromJSON_Entity_OverwritesField) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"target", {{"id", 55u}, {"generation", 2u}}}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_EQ(comp.target.id,         55u);
    EXPECT_EQ(comp.target.generation, 2u);
}

TEST(PropertySerializer, DeserializeFromJSON_TransientProperty_IsIgnored) {
    // Even if JSON contains the transient field, it must not be applied.
    TransientComponent comp;
    comp.visible = 1.0f;
    comp.hidden  = 99;
    const ClassInfo& info = TransientComponent::GetStaticClassInfo();

    nlohmann::json j = {{"visible", 7.7f}, {"hidden", 1000}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_TRUE(ok);
    EXPECT_FLOAT_EQ(comp.visible, 7.7f) << "'visible' must be updated";
    EXPECT_EQ(comp.hidden, 99)          << "'hidden' (Transient) must remain unchanged";
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 9 — Full JSON roundtrip
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertySerializer, JSONRoundtrip_AllTypes_FieldsMatchOriginal) {
    AllTypesComponent original;
    original.flag      = true;
    original.count     = -7;
    original.speed     = 2.5f;
    original.precision = 1.41421356237;
    original.label     = "roundtrip";
    original.uv        = Vec2{0.33f, 0.66f};
    original.position  = Vec3{-1.0f, 0.0f, 1.0f};
    original.color     = Vec4{0.2f, 0.4f, 0.6f, 0.8f};
    original.rotation  = Quat{0.0f, 0.7071f, 0.0f, 0.7071f};
    original.target.id         = 100u;
    original.target.generation = 5u;

    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();
    auto json = PropertySerializer::SerializeToJSON(&original, info);

    AllTypesComponent restored;
    bool ok = PropertySerializer::DeserializeFromJSON(&restored, info, json);
    ASSERT_TRUE(ok);

    EXPECT_EQ(restored.flag,      original.flag);
    EXPECT_EQ(restored.count,     original.count);
    EXPECT_FLOAT_EQ(restored.speed,     original.speed);
    EXPECT_DOUBLE_EQ(restored.precision, original.precision);
    EXPECT_EQ(restored.label,     original.label);

    EXPECT_FLOAT_EQ(restored.uv.x, original.uv.x);
    EXPECT_FLOAT_EQ(restored.uv.y, original.uv.y);

    EXPECT_FLOAT_EQ(restored.position.x, original.position.x);
    EXPECT_FLOAT_EQ(restored.position.y, original.position.y);
    EXPECT_FLOAT_EQ(restored.position.z, original.position.z);

    EXPECT_FLOAT_EQ(restored.color.x, original.color.x);
    EXPECT_FLOAT_EQ(restored.color.y, original.color.y);
    EXPECT_FLOAT_EQ(restored.color.z, original.color.z);
    EXPECT_FLOAT_EQ(restored.color.w, original.color.w);

    EXPECT_FLOAT_EQ(restored.rotation.x, original.rotation.x);
    EXPECT_FLOAT_EQ(restored.rotation.y, original.rotation.y);
    EXPECT_FLOAT_EQ(restored.rotation.z, original.rotation.z);
    EXPECT_FLOAT_EQ(restored.rotation.w, original.rotation.w);

    EXPECT_EQ(restored.target.id,         original.target.id);
    EXPECT_EQ(restored.target.generation, original.target.generation);
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 10 — DeserializeFromJSON: missing fields keep defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertySerializer, Deserialize_MissingFields_KeepDefaultValues) {
    AllTypesComponent comp;
    // Record the fresh default values.
    const int    default_count = comp.count;
    const float  default_speed = comp.speed;
    const std::string default_label = comp.label;

    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    // JSON only contains "flag" — all other fields are absent.
    nlohmann::json partial = {{"flag", true}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, partial);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(comp.flag);
    EXPECT_EQ(comp.count, default_count) << "count must keep default when absent from JSON";
    EXPECT_FLOAT_EQ(comp.speed, default_speed) << "speed must keep default when absent from JSON";
    EXPECT_EQ(comp.label, default_label) << "label must keep default when absent from JSON";
}

TEST(PropertySerializer, Deserialize_EmptyJsonObject_AllFieldsKeepDefaults) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, nlohmann::json::object());

    EXPECT_TRUE(ok);
    EXPECT_EQ(comp.count,  42)     << "Default count must be unchanged";
    EXPECT_FLOAT_EQ(comp.speed, 3.14f) << "Default speed must be unchanged";
    EXPECT_EQ(comp.label, "default")   << "Default label must be unchanged";
    EXPECT_FALSE(comp.flag)            << "Default flag must be false";
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 11 — DeserializeFromJSON: unknown fields are ignored
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertySerializer, Deserialize_UnknownFields_DoNotCrash_ValidFieldsApplied) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {
        {"count", 55},
        {"unknownField",  "should be ignored"},
        {"anotherExtra",  12345},
        {"__metadata",    {{"version", 1}}},
    };

    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    // Known field applied, engine must not crash.
    EXPECT_TRUE(ok);
    EXPECT_EQ(comp.count, 55);
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 12 — DeserializeFromJSON: type mismatches return false, field unchanged
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertySerializer, Deserialize_BoolField_WithStringValue_ReturnsFalse) {
    AllTypesComponent comp;
    comp.flag = false;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"flag", "not_a_bool"}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok)       << "Type mismatch must cause DeserializeFromJSON to return false";
    EXPECT_FALSE(comp.flag) << "Flag must remain unchanged on mismatch";
}

TEST(PropertySerializer, Deserialize_IntField_WithStringValue_ReturnsFalse) {
    AllTypesComponent comp;
    comp.count = 42;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"count", "not_an_int"}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    EXPECT_EQ(comp.count, 42) << "count must remain unchanged on mismatch";
}

TEST(PropertySerializer, Deserialize_StringField_WithIntValue_ReturnsFalse) {
    AllTypesComponent comp;
    comp.label = "original";
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"label", 999}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    EXPECT_EQ(comp.label, "original") << "label must remain unchanged on mismatch";
}

TEST(PropertySerializer, Deserialize_Vec3Field_WithScalar_ReturnsFalse) {
    AllTypesComponent comp;
    comp.position = Vec3{1.0f, 2.0f, 3.0f};
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    nlohmann::json j = {{"position", 42}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    EXPECT_FLOAT_EQ(comp.position.x, 1.0f) << "position.x must remain unchanged";
}

TEST(PropertySerializer, Deserialize_Vec3Field_WithWrongArraySize_ReturnsFalse) {
    AllTypesComponent comp;
    comp.position = Vec3{1.0f, 2.0f, 3.0f};
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    // Two-element array for a Vec3 field.
    nlohmann::json j = {{"position", nlohmann::json::array({1.0f, 2.0f})}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    EXPECT_FLOAT_EQ(comp.position.z, 3.0f) << "position.z must remain unchanged";
}

TEST(PropertySerializer, Deserialize_Vec2Field_WithWrongArraySize_ReturnsFalse) {
    AllTypesComponent comp;
    comp.uv = Vec2{0.5f, 0.5f};
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    // Three-element array for a Vec2 field.
    nlohmann::json j = {{"uv", nlohmann::json::array({0.1f, 0.2f, 0.3f})}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    EXPECT_FLOAT_EQ(comp.uv.x, 0.5f);
}

TEST(PropertySerializer, Deserialize_QuatField_WithWrongArraySize_ReturnsFalse) {
    AllTypesComponent comp;
    comp.rotation = Quat::Identity();
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    // Three-element array for a Quat field (needs 4).
    nlohmann::json j = {{"rotation", nlohmann::json::array({0.0f, 0.0f, 1.0f})}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    // w component of identity must remain 1.0.
    EXPECT_FLOAT_EQ(comp.rotation.w, 1.0f);
}

TEST(PropertySerializer, Deserialize_EntityField_MissingIdKey_ReturnsFalse) {
    AllTypesComponent comp;
    comp.target.id         = 10u;
    comp.target.generation = 1u;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    // Object missing "id" key.
    nlohmann::json j = {{"target", {{"generation", 2u}}}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    EXPECT_EQ(comp.target.id, 10u) << "target.id must remain unchanged";
}

TEST(PropertySerializer, Deserialize_EntityField_NotAnObject_ReturnsFalse) {
    AllTypesComponent comp;
    comp.target.id = 10u;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    // Entity must be a JSON object; passing an integer is a mismatch.
    nlohmann::json j = {{"target", 999}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok);
    EXPECT_EQ(comp.target.id, 10u);
}

TEST(PropertySerializer, Deserialize_NonObjectJSON_ReturnsFalse) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, nlohmann::json::array());
    EXPECT_FALSE(ok) << "Non-object JSON root must cause DeserializeFromJSON to return false";
}

TEST(PropertySerializer, Deserialize_NullJSON_ReturnsFalse) {
    AllTypesComponent comp;
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, nlohmann::json(nullptr));
    EXPECT_FALSE(ok);
}

// ─────────────────────────────────────────────────────────────────────────────
// Section 13 — Partial deserialization: valid + mismatched fields in same object
// ─────────────────────────────────────────────────────────────────────────────

TEST(PropertySerializer, Deserialize_MixedValidAndMismatch_AppliesValidReturnsFalse) {
    AllTypesComponent comp;
    comp.count = 0;
    comp.label = "before";
    const ClassInfo& info = AllTypesComponent::GetStaticClassInfo();

    // "count" is valid; "label" has wrong type.
    nlohmann::json j = {{"count", 88}, {"label", 123}};
    bool ok = PropertySerializer::DeserializeFromJSON(&comp, info, j);

    EXPECT_FALSE(ok)        << "Partial failure must cause overall false return";
    EXPECT_EQ(comp.count, 88)      << "Valid field 'count' must still be applied";
    EXPECT_EQ(comp.label, "before") << "Mismatched 'label' must remain unchanged";
}
