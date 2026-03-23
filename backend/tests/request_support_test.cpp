/// InsightOS backend — request-normalization tests.

#include "insightos/backend/request_support.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

#define TEST(name)                                                         \
    void test_##name();                                                    \
    struct Register_##name {                                               \
        Register_##name() { tests().push_back({#name, test_##name}); }     \
    } register_##name;                                                     \
    void test_##name()

struct TestEntry {
    const char* name;
    void (*fn)();
};

std::vector<TestEntry>& tests() {
    static std::vector<TestEntry> entries;
    return entries;
}

#define EXPECT_EQ(a, b)                                                    \
    do {                                                                   \
        if ((a) != (b)) {                                                  \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected: " << (b)                         \
                      << "\n    got:      " << (a) << "\n";                \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define EXPECT_TRUE(x)                                                     \
    do {                                                                   \
        if (!(x)) {                                                        \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected true\n";                          \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

#define EXPECT_FALSE(x)                                                    \
    do {                                                                   \
        if ((x)) {                                                         \
            std::cerr << "  FAIL at " << __FILE__ << ":" << __LINE__       \
                      << "\n    expected false\n";                         \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)

using namespace insightos::backend;

TEST(normalize_source_input_matches_http_and_insightos_inputs) {
    auto uri_res = normalize_source_input(
        "insightos://localhost/front-camera/1080p_30/mjpeg"
        "?audio_rate=48000&must_match=true");
    EXPECT_TRUE(uri_res.ok());
    EXPECT_TRUE(uri_res.value().is_local);
    EXPECT_EQ(uri_res.value().canonical_uri,
              "insightos://localhost/front-camera/1080p_30/mjpeg"
              "?audio_rate=48000&must_match=true");
    EXPECT_EQ(uri_res.value().request.selector.name, "front-camera");
    EXPECT_EQ(uri_res.value().request.preset_name, "1080p_30");
    EXPECT_TRUE(uri_res.value().request.delivery_name.has_value());
    EXPECT_EQ(*uri_res.value().request.delivery_name, "mjpeg");
    EXPECT_TRUE(uri_res.value().request.overrides.audio_rate.has_value());
    EXPECT_EQ(*uri_res.value().request.overrides.audio_rate, 48000u);
    EXPECT_TRUE(uri_res.value().request.overrides.must_match);

    auto http_res = normalize_source_input(
        "http://localhost:18180/front-camera/1080p_30/mjpeg"
        "?audio_rate=48000&must_match=true");
    EXPECT_TRUE(http_res.ok());
    EXPECT_EQ(http_res.value().canonical_uri, uri_res.value().canonical_uri);
    EXPECT_EQ(http_res.value().request.selector.name,
              uri_res.value().request.selector.name);
    EXPECT_EQ(http_res.value().request.preset_name,
              uri_res.value().request.preset_name);
    EXPECT_EQ(*http_res.value().request.delivery_name,
              *uri_res.value().request.delivery_name);
    EXPECT_EQ(*http_res.value().request.overrides.audio_rate,
              *uri_res.value().request.overrides.audio_rate);
    EXPECT_EQ(http_res.value().request.overrides.must_match,
              uri_res.value().request.overrides.must_match);
}

TEST(normalize_source_input_accepts_d2c_query_alias) {
    auto res = normalize_source_input(
        "insightos://lab-box/desk-rgbd/480p_30?d2c=off");
    EXPECT_TRUE(res.ok());
    EXPECT_TRUE(res.value().request.overrides.depth_alignment.has_value());
    EXPECT_EQ(*res.value().request.overrides.depth_alignment, "off");
    EXPECT_EQ(res.value().request_json["overrides"]["d2c"], "off");
}

TEST(session_request_from_json_rejects_invalid_override_type) {
    nlohmann::json body = {
        {"name", "front-camera"},
        {"preset", "1080p_30"},
        {"overrides", {
            {"must_match", "maybe"},
        }},
    };

    auto res = session_request_from_json(body);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.error().code, "bad_request");
    EXPECT_EQ(res.error().message, "overrides.must_match must be boolean");
}

TEST(session_request_from_json_accepts_d2c_alias) {
    nlohmann::json body = {
        {"name", "desk-rgbd"},
        {"preset", "480p_30"},
        {"overrides", {
            {"d2c", "hardware"},
        }},
    };

    auto res = session_request_from_json(body);
    EXPECT_TRUE(res.ok());
    EXPECT_TRUE(res.value().delivery_name == std::nullopt);
    EXPECT_TRUE(res.value().overrides.depth_alignment.has_value());
    EXPECT_EQ(*res.value().overrides.depth_alignment, "hardware");
}

}  // namespace

int main() {
    std::cout << "Running " << tests().size() << " request support tests...\n";
    for (const auto& test : tests()) {
        std::cout << "- " << test.name << "\n";
        test.fn();
    }
    std::cout << "All request support tests passed.\n";
    return 0;
}
