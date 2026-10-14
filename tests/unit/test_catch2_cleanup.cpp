// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QPointer>

#include <memory>

namespace
{

class Sender : public QObject
{
public:
    int connectionCount() const
    {
        return receivers(SIGNAL(destroyed(QObject *)));
    }
};

class Receiver : public QObject
{
public:
    explicit Receiver(bool &destroyed)
        : m_destroyed(destroyed)
    {
    }

    ~Receiver() override
    {
        m_destroyed = true;
    }

private:
    bool &m_destroyed;
};

struct CleanupState {
    Sender sender;
    QPointer<Receiver> receiver;
    bool entered = false;
    bool destroyed = false;
};

CleanupState failureState;

} // namespace

TEST_CASE("Passing REQUIRE cleans up a scoped receiver", "[catch2-cleanup-control]")
{
    CleanupState state;
    {
        auto receiver = std::make_unique<Receiver>(state.destroyed);
        state.receiver = receiver.get();
        REQUIRE(QObject::connect(&state.sender, &QObject::destroyed, receiver.get(), [](QObject *) { }));
        REQUIRE(state.sender.connectionCount() == 1);
    }

    CHECK(state.destroyed);
    CHECK(state.receiver.isNull());
    CHECK(state.sender.connectionCount() == 0);
}

// These hidden cases run together, in lexicographic order, in one manual CTest.
TEST_CASE("Catch2 cleanup 1: fatal REQUIRE", "[.][catch2-cleanup][!shouldfail]")
{
    // Heap ownership keeps the old no-exception path free of dangling stack objects.
    auto receiver = std::make_unique<Receiver>(failureState.destroyed);
    failureState.receiver = receiver.get();
    REQUIRE(QObject::connect(&failureState.sender, &QObject::destroyed, receiver.get(), [](QObject *) { }));
    REQUIRE(failureState.sender.connectionCount() == 1);
    failureState.entered = true;
    REQUIRE(false);
}

TEST_CASE("Catch2 cleanup 2: following case has no retained receiver", "[.][catch2-cleanup]")
{
    REQUIRE(failureState.entered);
    CHECK(failureState.destroyed);
    CHECK(failureState.receiver.isNull());
    CHECK(failureState.sender.connectionCount() == 0);
}
