#include <gtest/gtest.h>
#include "result.hpp"
#include <string>

using namespace syspulse;

TEST(ResultTest, OkValue) {
    Result<int, std::string> res = Ok(42);
    EXPECT_TRUE(res.is_ok());
    EXPECT_FALSE(res.is_err());
    EXPECT_TRUE(static_cast<bool>(res));
    EXPECT_EQ(res.value(), 42);
    EXPECT_EQ(res.unwrap(), 42);
}

TEST(ResultTest, ErrorValue) {
    Result<int, std::string> res = Error<int>("Failed to connect");
    EXPECT_FALSE(res.is_ok());
    EXPECT_TRUE(res.is_err());
    EXPECT_FALSE(static_cast<bool>(res));
    EXPECT_EQ(res.error(), "Failed to connect");
    EXPECT_THROW((void)res.value(), std::runtime_error);
    EXPECT_EQ(res.unwrap_or(100), 100);
}

TEST(ResultTest, MapOperation) {
    Result<int, std::string> ok_res = Ok(21);
    auto mapped = ok_res.map([](int x) { return x * 2; });
    EXPECT_TRUE(mapped.is_ok());
    EXPECT_EQ(mapped.value(), 42);

    Result<int, std::string> err_res = Error<int>("error");
    auto mapped_err = err_res.map([](int x) { return x * 2; });
    EXPECT_TRUE(mapped_err.is_err());
    EXPECT_EQ(mapped_err.error(), "error");
}

TEST(ResultTest, VoidSpecialization) {
    Result<void, std::string> ok_res = Ok();
    EXPECT_TRUE(ok_res.is_ok());
    EXPECT_FALSE(ok_res.is_err());
    EXPECT_NO_THROW(ok_res.value());

    Result<void, std::string> err_res = Error<void>("permission denied");
    EXPECT_TRUE(err_res.is_err());
    EXPECT_FALSE(err_res.is_ok());
    EXPECT_EQ(err_res.error(), "permission denied");
    EXPECT_THROW(err_res.value(), std::runtime_error);
}
