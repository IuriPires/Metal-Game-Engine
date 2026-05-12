#include "mge/memory/arena.h"
#include "mge/memory/pool.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>

using mge::memory::Arena;
using mge::memory::OwnedArena;
using mge::memory::OwnedPool;
using mge::memory::Pool;

TEST_CASE("Arena bumps with correct alignment") {
    OwnedArena arena(1024, 64);
    void*      p1 = arena.allocate(13, 1);
    void*      p2 = arena.allocate(8,  16);
    void*      p3 = arena.allocate(32, 32);
    REQUIRE(p1 != nullptr);
    REQUIRE(p2 != nullptr);
    REQUIRE(p3 != nullptr);
    CHECK((reinterpret_cast<std::uintptr_t>(p2) % 16) == 0);
    CHECK((reinterpret_cast<std::uintptr_t>(p3) % 32) == 0);
    CHECK(arena.stats().num_alloc_calls == 3);
}

TEST_CASE("Arena returns nullptr on exhaustion") {
    OwnedArena arena(64, 16);
    void*      p = arena.allocate(32);
    REQUIRE(p != nullptr);
    void* big = arena.allocate(128);
    CHECK(big == nullptr);
}

TEST_CASE("Arena reset is O(1) and reusable") {
    OwnedArena arena(256, 16);
    void*      a = arena.allocate(64);
    REQUIRE(a != nullptr);
    CHECK(arena.bytes_used() == 64);
    arena.reset();
    CHECK(arena.bytes_used() == 0);
    void* b = arena.allocate(64);
    CHECK(b == a);  // bump head went back to base
    CHECK(arena.stats().bytes_high_water >= 64);
}

TEST_CASE("Arena emplace constructs in place") {
    struct Probe {
        int a;
        float b;
    };
    OwnedArena arena(64, 16);
    Probe*     p = arena.emplace<Probe>(7, 1.5f);
    REQUIRE(p != nullptr);
    CHECK(p->a == 7);
    CHECK(p->b == doctest::Approx(1.5));
}

TEST_CASE("Pool allocates and reuses freed slots") {
    OwnedPool pool(sizeof(int), 4, alignof(int));
    CHECK(pool.capacity() == 4);

    std::array<void*, 4> slots{};
    for (auto& s : slots) {
        s = pool.allocate();
        REQUIRE(s != nullptr);
    }
    // exhausted
    CHECK(pool.allocate() == nullptr);

    pool.free(slots[1]);
    void* reuse = pool.allocate();
    CHECK(reuse == slots[1]);
}

TEST_CASE("Pool emplace + destroy lifecycle") {
    struct Probe {
        int v;
    };
    OwnedPool pool(sizeof(Probe), 8, alignof(Probe));
    Probe*    a = pool.emplace<Probe>(11);
    Probe*    b = pool.emplace<Probe>(22);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->v == 11);
    CHECK(b->v == 22);
    CHECK(pool.stats().num_live_allocs == 2);
    pool.destroy(a);
    pool.destroy(b);
    CHECK(pool.stats().num_live_allocs == 0);
}

TEST_CASE("Pool slot size respects alignment") {
    OwnedPool pool(8, 16, 64);
    for (std::size_t i = 0; i < 16; ++i) {
        void* p = pool.allocate();
        REQUIRE(p != nullptr);
        CHECK((reinterpret_cast<std::uintptr_t>(p) % 64) == 0);
    }
}
