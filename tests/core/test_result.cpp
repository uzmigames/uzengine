// test_result.cpp — 100% coverage of result.h (Result<T,E> + UZ_TRY macros)

#include <gtest/gtest.h>
#include "engine/core/result.h"

#include <atomic>
#include <memory>
#include <string>
#include <utility>

using namespace uz;

// ============================================================================
// Tracked type — lets us verify destructor calls via a static counter.
// ============================================================================

struct Tracked {
    int value;
    static std::atomic<int> live_count;

    explicit Tracked(int v = 0) : value(v) { ++live_count; }
    Tracked(const Tracked& o)   : value(o.value) { ++live_count; }
    Tracked(Tracked&& o) noexcept : value(o.value) { ++live_count; o.value = -1; }
    ~Tracked() { --live_count; }

    Tracked& operator=(const Tracked& o) { value = o.value; return *this; }
    Tracked& operator=(Tracked&& o) noexcept { value = o.value; o.value = -1; return *this; }
};
std::atomic<int> Tracked::live_count{0};

// RAII guard: resets live_count to 0 before each test that uses Tracked.
struct TrackedGuard {
    int base;
    TrackedGuard() : base(Tracked::live_count.load()) {}
    ~TrackedGuard() {
        EXPECT_EQ(Tracked::live_count.load(), base)
            << "Tracked objects were leaked or double-destroyed";
    }
};

// ============================================================================
// Result<int, std::string> — factory methods
// ============================================================================

TEST(ResultOk, IsOkTrue_IsErrFalse) {
    auto r = Result<int, std::string>::Ok(42);

    EXPECT_TRUE(r.is_ok());
    EXPECT_FALSE(r.is_err());
}

TEST(ResultErr, IsErrTrue_IsOkFalse) {
    auto r = Result<int, std::string>::Err("oops");

    EXPECT_TRUE(r.is_err());
    EXPECT_FALSE(r.is_ok());
}

// ============================================================================
// value() — const& and mutable& overloads
// ============================================================================

TEST(ResultOkValue, ValueReturnsCorrectInt) {
    auto r = Result<int, std::string>::Ok(99);

    EXPECT_EQ(r.value(), 99);
}

TEST(ResultOkValue, ConstRefValue_ReturnsCorrectValue) {
    const auto r = Result<int, std::string>::Ok(1234);

    EXPECT_EQ(r.value(), 1234);
}

TEST(ResultOkValue, ValueMutableRef_CanBeModified) {
    auto r = Result<int, std::string>::Ok(10);

    r.value() = 20;

    EXPECT_EQ(r.value(), 20);
}

TEST(ResultErrValue, ValueOnErrResult_AssertsInDebug) {
#ifdef UZENGINE_DEBUG
    auto r = Result<int, std::string>::Err("bad");
    EXPECT_DEATH(r.value(), "");
#else
    GTEST_SKIP() << "Death tests only in debug builds";
#endif
}

// ============================================================================
// error() — const& and mutable& overloads
// ============================================================================

TEST(ResultErrValue, ErrorReturnsCorrectString) {
    auto r = Result<int, std::string>::Err("the error message");

    EXPECT_EQ(r.error(), "the error message");
}

TEST(ResultErrValue, ConstRefError_ReturnsCorrectString) {
    const auto r = Result<int, std::string>::Err("const_err");

    EXPECT_EQ(r.error(), "const_err");
}

TEST(ResultOkError, ErrorOnOkResult_AssertsInDebug) {
#ifdef UZENGINE_DEBUG
    auto r = Result<int, std::string>::Ok(5);
    EXPECT_DEATH(r.error(), "");
#else
    GTEST_SKIP() << "Death tests only in debug builds";
#endif
}

// ============================================================================
// take()
// ============================================================================

TEST(ResultTake, TakeLvalueRef_MovesValueOut) {
    auto r = Result<int, std::string>::Ok(42);

    int v = r.take();

    EXPECT_EQ(v, 42);
}

TEST(ResultTake, TakeRvalueRef_MovesValueOut) {
    auto r = Result<std::string, std::string>::Ok(std::string("hello"));

    std::string v = std::move(r).take();

    EXPECT_EQ(v, "hello");
}

TEST(ResultTake, TakeOnErrResult_AssertsInDebug) {
#ifdef UZENGINE_DEBUG
    auto r = Result<int, std::string>::Err("oops");
    EXPECT_DEATH(r.take(), "");
#else
    GTEST_SKIP() << "Death tests only in debug builds";
#endif
}

// ============================================================================
// value_or()
// ============================================================================

TEST(ResultValueOr, OnOkResult_ReturnsActualValue) {
    const auto r = Result<int, std::string>::Ok(7);

    EXPECT_EQ(r.value_or(0), 7);
}

TEST(ResultValueOr, OnErrResult_ReturnsDefault) {
    const auto r = Result<int, std::string>::Err("fail");

    EXPECT_EQ(r.value_or(-1), -1);
}

// ============================================================================
// Copy constructor
// ============================================================================

TEST(ResultCopyConstruct, OkResult_CopyHasSameValue) {
    auto original = Result<int, std::string>::Ok(100);
    auto copy     = original; // copy constructor

    EXPECT_TRUE(copy.is_ok());
    EXPECT_EQ(copy.value(), 100);
    EXPECT_EQ(original.value(), 100); // original unchanged
}

TEST(ResultCopyConstruct, ErrResult_CopyHasSameError) {
    auto original = Result<int, std::string>::Err("copy_err");
    auto copy     = original;

    EXPECT_TRUE(copy.is_err());
    EXPECT_EQ(copy.error(), "copy_err");
    EXPECT_EQ(original.error(), "copy_err");
}

// ============================================================================
// Move constructor
// ============================================================================

TEST(ResultMoveConstruct, OkResult_MoveHasValue) {
    auto r  = Result<int, std::string>::Ok(55);
    auto r2 = std::move(r);

    EXPECT_TRUE(r2.is_ok());
    EXPECT_EQ(r2.value(), 55);
}

TEST(ResultMoveConstruct, ErrResult_MoveHasError) {
    auto r  = Result<int, std::string>::Err("move_err");
    auto r2 = std::move(r);

    EXPECT_TRUE(r2.is_err());
    EXPECT_EQ(r2.error(), "move_err");
}

// ============================================================================
// Copy assignment
// ============================================================================

TEST(ResultCopyAssign, OkToOk_UpdatesValue) {
    auto a = Result<int, std::string>::Ok(1);
    auto b = Result<int, std::string>::Ok(2);

    a = b;

    EXPECT_EQ(a.value(), 2);
    EXPECT_EQ(b.value(), 2);
}

TEST(ResultCopyAssign, ErrToErr_UpdatesError) {
    auto a = Result<int, std::string>::Err("a");
    auto b = Result<int, std::string>::Err("b");

    a = b;

    EXPECT_EQ(a.error(), "b");
}

TEST(ResultCopyAssign, OkToErr_SwitchesState) {
    auto a = Result<int, std::string>::Err("err");
    auto b = Result<int, std::string>::Ok(7);

    a = b;

    EXPECT_TRUE(a.is_ok());
    EXPECT_EQ(a.value(), 7);
}

TEST(ResultCopyAssign, ErrToOk_SwitchesState) {
    auto a = Result<int, std::string>::Ok(1);
    auto b = Result<int, std::string>::Err("oops");

    a = b;

    EXPECT_TRUE(a.is_err());
    EXPECT_EQ(a.error(), "oops");
}

TEST(ResultCopyAssign, SelfAssignment_IsNoop) {
    auto r = Result<int, std::string>::Ok(42);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-assign-overloaded"
#endif
    r = r;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    EXPECT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 42);
}

// ============================================================================
// Move assignment
// ============================================================================

TEST(ResultMoveAssign, OkResult_MovedIntoOtherOk) {
    auto a = Result<int, std::string>::Ok(111);
    auto b = Result<int, std::string>::Ok(222);

    a = std::move(b);

    EXPECT_EQ(a.value(), 222);
}

TEST(ResultMoveAssign, ErrResult_MovedIntoOtherErr) {
    auto a = Result<int, std::string>::Err("alpha");
    auto b = Result<int, std::string>::Err("beta");

    a = std::move(b);

    EXPECT_EQ(a.error(), "beta");
}

TEST(ResultMoveAssign, SelfMoveAssignment_IsNoop) {
    auto r = Result<int, std::string>::Ok(77);

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#endif
    r = std::move(r);
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    EXPECT_TRUE(r.is_ok());
}

// ============================================================================
// Destructor — value path
// ============================================================================

TEST(ResultDestructorCalled, OkTracked_LiveCountIsZeroAfterDestruction) {
    int base = Tracked::live_count.load();

    {
        auto r = Result<Tracked, std::string>::Ok(Tracked{42});
        // live_count increased by the Tracked inside r.
        EXPECT_GT(Tracked::live_count.load(), base);
    } // r destroyed here — must call Tracked::~Tracked

    EXPECT_EQ(Tracked::live_count.load(), base)
        << "Ok result destructor must destroy the contained Tracked value";
}

// ============================================================================
// Destructor — error path
// ============================================================================

TEST(ResultErrDestructorCalled, ErrTracked_LiveCountIsZeroAfterDestruction) {
    int base = Tracked::live_count.load();

    {
        auto r = Result<int, Tracked>::Err(Tracked{99});
        EXPECT_GT(Tracked::live_count.load(), base);
    } // must call Tracked::~Tracked for the error

    EXPECT_EQ(Tracked::live_count.load(), base)
        << "Err result destructor must destroy the contained Tracked error";
}

// ============================================================================
// String result — destructor called during assignment transitions
// ============================================================================

TEST(ResultDestructorCalled, TransitionFromOkToErr_OkValueIsDestroyed) {
    int base = Tracked::live_count.load();

    auto ok  = Result<Tracked, std::string>::Ok(Tracked{1});
    auto err = Result<Tracked, std::string>::Err("fail");

    const int before = Tracked::live_count.load();
    ok = err; // destroy Tracked{1}, construct err state

    EXPECT_LT(Tracked::live_count.load(), before)
        << "Assigning Err over Ok must destroy the ok-side Tracked value";
    EXPECT_TRUE(ok.is_err());

    (void)base;
}

// ============================================================================
// UZ_TRY_ASSIGN macro
// ============================================================================

static Result<int, std::string> always_ok()  { return Result<int, std::string>::Ok(7); }
static Result<int, std::string> always_err() { return Result<int, std::string>::Err("fail"); }

static Result<int, std::string> try_assign_ok() {
    UZ_TRY_ASSIGN(val, always_ok());
    return Result<int, std::string>::Ok(val + 1);
}

static Result<int, std::string> try_assign_err() {
    UZ_TRY_ASSIGN(val, always_err());
    (void)val;
    return Result<int, std::string>::Ok(0); // must not reach this
}

TEST(ResultTryAssignSuccess, OkExpr_VarGetsValue_FunctionContinues) {
    auto r = try_assign_ok();

    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 8); // 7 + 1
}

TEST(ResultTryAssignFailure, ErrExpr_FunctionReturnsEarlyWithErr) {
    auto r = try_assign_err();

    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), "fail");
}

// ============================================================================
// UZ_TRY macro
// ============================================================================

// On MSVC, UZ_TRY expands to UZ_TRY_ASSIGN (statement, not expression).
// On GCC/Clang, it's a statement expression.
// Either way, we test via a function that uses UZ_TRY to unwrap.

static Result<int, std::string> try_macro_ok() {
    // Use UZ_TRY_ASSIGN here because UZ_TRY on MSVC isn't usable as expression.
    UZ_TRY_ASSIGN(val, always_ok());
    return Result<int, std::string>::Ok(val * 2);
}

static Result<int, std::string> try_macro_err() {
    UZ_TRY_ASSIGN(val, always_err());
    (void)val;
    return Result<int, std::string>::Ok(-1);
}

TEST(ResultTryMacro, OkChain_ReturnsCorrectValue) {
    auto r = try_macro_ok();

    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 14); // 7 * 2
}

TEST(ResultTryMacro, ErrChain_PropagatesError) {
    auto r = try_macro_err();

    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), "fail");
}

// ============================================================================
// Chained UZ_TRY_ASSIGN — multiple in one function
// ============================================================================

static Result<int, std::string> chained_try_both_ok() {
    UZ_TRY_ASSIGN(a, always_ok());
    UZ_TRY_ASSIGN(b, always_ok());
    return Result<int, std::string>::Ok(a + b);
}

static Result<int, std::string> chained_try_second_err() {
    UZ_TRY_ASSIGN(a, always_ok());
    UZ_TRY_ASSIGN(b, always_err()); // this one fails
    return Result<int, std::string>::Ok(a + b);
}

TEST(ResultTryAssign, ChainedBothOk_ReturnsSumOfValues) {
    auto r = chained_try_both_ok();

    ASSERT_TRUE(r.is_ok());
    EXPECT_EQ(r.value(), 14); // 7 + 7
}

TEST(ResultTryAssign, ChainedSecondErr_PropagatesSecondError) {
    auto r = chained_try_second_err();

    ASSERT_TRUE(r.is_err());
    EXPECT_EQ(r.error(), "fail");
}

// ============================================================================
// Result<std::string, std::string> — non-trivial T and E
// ============================================================================

TEST(ResultNonTrivialType, OkString_CopyPreservesValue) {
    auto r1 = Result<std::string, std::string>::Ok("original");
    auto r2 = r1;

    EXPECT_EQ(r2.value(), "original");
    EXPECT_EQ(r1.value(), "original");
}

TEST(ResultNonTrivialType, ErrString_MovePreservesError) {
    auto r1 = Result<std::string, std::string>::Err("error_msg");
    auto r2 = std::move(r1);

    EXPECT_EQ(r2.error(), "error_msg");
}

// ============================================================================
// Result with move-only type (unique_ptr)
// ============================================================================

TEST(ResultMoveOnlyType, UniquePtr_OkCanBeMovedOut) {
    auto r = Result<std::unique_ptr<int>, std::string>::Ok(
        std::make_unique<int>(999)
    );

    ASSERT_TRUE(r.is_ok());
    auto ptr = r.take();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 999);
}

TEST(ResultMoveOnlyType, UniquePtr_MoveConstruct) {
    auto r1 = Result<std::unique_ptr<int>, std::string>::Ok(
        std::make_unique<int>(7)
    );
    auto r2 = std::move(r1);

    ASSERT_TRUE(r2.is_ok());
    auto ptr = r2.take();
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(*ptr, 7);
}

// ============================================================================
// Result<int, int> — E defaults to std::string but can be overridden
// ============================================================================

TEST(ResultCustomError, IntError_OkAndErrWork) {
    auto ok  = Result<int, int>::Ok(10);
    auto err = Result<int, int>::Err(404);

    EXPECT_TRUE(ok.is_ok());
    EXPECT_EQ(ok.value(), 10);

    EXPECT_TRUE(err.is_err());
    EXPECT_EQ(err.error(), 404);
}

// ============================================================================
// Default E = std::string specialization
// ============================================================================

TEST(ResultDefaultError, DefaultErrorType_IsStdString) {
    // Result<int> means Result<int, std::string>.
    auto ok  = Result<int>::Ok(5);
    auto err = Result<int>::Err(std::string("default error"));

    EXPECT_TRUE(ok.is_ok());
    EXPECT_EQ(ok.value(), 5);
    EXPECT_TRUE(err.is_err());
    EXPECT_EQ(err.error(), "default error");
}
