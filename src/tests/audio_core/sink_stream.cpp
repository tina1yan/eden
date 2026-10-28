// SPDX-License-Identifier: GPL-3.0-or-later
// Regression tests developed with AI assistance from a captured audio shutdown hang.

#include <array>
#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <thread>
#include <catch2/catch_test_macros.hpp>
#include "audio_core/sink/sink_stream.h"
#include "common/fs/path_util.h"
#include "core/core.h"

namespace AudioCore::Sink {
namespace {
using namespace std::chrono_literals;

class TestStream final : public SinkStream {
public:
    explicit TestStream(Core::System& system) : SinkStream{system, StreamType::Render} {
        paused = false;
        SetRingSize(4);
    }
};

struct Fixture {
    Fixture() {
        previous_nand = Common::FS::GetEdenPath(Common::FS::EdenPath::NANDDir);
        temporary_nand = std::filesystem::temp_directory_path() /
                         ("eden-sink-test-" + std::to_string(
                             std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(temporary_nand);
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, temporary_nand);
        system = std::make_unique<Core::System>();
        stream = std::make_unique<TestStream>(*system);
    }

    ~Fixture() {
        stream.reset();
        system.reset();
        Common::FS::SetEdenPath(Common::FS::EdenPath::NANDDir, previous_nand);
        std::filesystem::remove_all(temporary_nand);
    }

    std::filesystem::path previous_nand;
    std::filesystem::path temporary_nand;
    std::unique_ptr<Core::System> system;
    std::unique_ptr<TestStream> stream;
};
} // namespace

TEST_CASE("SinkStream stops accepting buffers during shutdown", "[audio][shutdown]") {
    Fixture fixture;
    SinkBuffer buffer{};
    fixture.stream->AppendBuffer(buffer, {});
    REQUIRE(fixture.stream->GetQueueSize() == 1);
    fixture.system->SetShuttingDown(true);
    fixture.stream->AppendBuffer(buffer, {});
    CHECK(fixture.stream->GetQueueSize() == 1);

    std::array<s16, 2> output{123, 456};
    fixture.stream->ProcessAudioOutAndRender(output, 1);
    CHECK(output == std::array<s16, 2>{0, 0});
    CHECK(fixture.stream->GetQueueSize() == 1);
}

TEST_CASE("SinkStream releases a full-queue producer on shutdown", "[audio][shutdown]") {
    Fixture fixture;
    SinkBuffer buffer{};
    constexpr u32 capacity = 0x10000;
    for (u32 i = 0; i < capacity; ++i) {
        fixture.stream->AppendBuffer(buffer, {});
    }
    REQUIRE(fixture.stream->GetQueueSize() == capacity);

    std::promise<void> entered;
    auto entering = entered.get_future();
    auto producer = std::async(std::launch::async, [&] {
        entered.set_value();
        fixture.stream->AppendBuffer(buffer, {});
    });
    entering.wait();
    CHECK(producer.wait_for(50ms) == std::future_status::timeout);

    SECTION("audio callback has already stopped") {
        fixture.system->SetShuttingDown(true);
    }
    SECTION("audio callback continues to emit silence") {
        fixture.system->SetShuttingDown(true);
        std::array<s16, 2> output{};
        fixture.stream->ProcessAudioOutAndRender(output, 1);
    }

    const bool finished = producer.wait_for(1s) == std::future_status::ready;
    const auto pending = fixture.stream->GetQueueSize();
    // Let a broken implementation return as well, so the regression reports a failure.
    if (!finished) {
        fixture.stream->ClearQueue();
    }
    producer.get();
    CHECK(finished);
    CHECK(pending == capacity);
}

TEST_CASE("SinkStream back pressure observes shutdown without callbacks", "[audio][shutdown]") {
    Fixture fixture;
    SinkBuffer buffer{};
    for (u32 i = 0; i < 8; ++i) {
        fixture.stream->AppendBuffer(buffer, {});
    }
    std::stop_source stop;
    auto waiter = std::async(std::launch::async, [&] {
        fixture.stream->WaitFreeSpace(stop.get_token());
    });
    CHECK(waiter.wait_for(50ms) == std::future_status::timeout);
    fixture.system->SetShuttingDown(true);
    const bool finished = waiter.wait_for(1s) == std::future_status::ready;
    stop.request_stop();
    waiter.get();
    CHECK(finished);
}

TEST_CASE("SinkStream still plays queued stereo samples", "[audio]") {
    Fixture fixture;
    SinkBuffer buffer{.frames = 2};
    std::array<s16, 4> samples{1000, -1000, 2000, -2000};
    fixture.stream->AppendBuffer(buffer, samples);
    REQUIRE(fixture.stream->GetQueueSize() == 1);
    std::array<s16, 4> output{};
    fixture.stream->ProcessAudioOutAndRender(output, 2);
    CHECK(output == samples);
    CHECK(fixture.stream->GetQueueSize() == 0);
}

TEST_CASE("SinkStream still waits for playback to make space", "[audio]") {
    Fixture fixture;
    SinkBuffer buffer{.frames = 1};
    std::array<s16, 2> samples{1000, -1000};
    for (u32 i = 0; i < 8; ++i) {
        fixture.stream->AppendBuffer(buffer, samples);
    }
    std::stop_source stop;
    auto waiter = std::async(std::launch::async, [&] {
        fixture.stream->WaitFreeSpace(stop.get_token());
    });
    CHECK(waiter.wait_for(50ms) == std::future_status::timeout);
    std::array<s16, 12> output{};
    fixture.stream->ProcessAudioOutAndRender(output, 6);
    const bool finished = waiter.wait_for(1s) == std::future_status::ready;
    stop.request_stop();
    waiter.get();
    CHECK(finished);
    CHECK(fixture.stream->GetQueueSize() == 2);
    for (std::size_t i = 0; i < output.size(); ++i) {
        CHECK(output[i] == samples[i % samples.size()]);
    }
}
} // namespace AudioCore::Sink
