#define NOMINMAX
#include <windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>
#include "../src/ui_affine_clip.h"

namespace {
using Matrix = std::array<float, 16>;
using Bytes = std::array<unsigned char, 0x800>;
unsigned long long checks = 0;
#define REQUIRE(condition) do { ++checks; if (!(condition)) { \
    std::printf("FAIL line %d: %s\n", __LINE__, #condition); return false; } } while (0)

Matrix Identity() {
    Matrix m{};
    m[0] = m[5] = m[10] = m[15] = 1;
    return m;
}
Matrix Product(const Matrix& a, const Matrix& b) {
    Matrix result{};
    for (unsigned r = 0; r != 4; ++r)
        for (unsigned c = 0; c != 4; ++c) {
            double sum = 0;
            for (unsigned k = 0; k != 4; ++k) sum += double(a[r * 4 + k]) * b[k * 4 + c];
            result[r * 4 + c] = static_cast<float>(sum);
        }
    return result;
}
std::array<double, 4> Point(const std::array<double, 4>& point, const Matrix& m) {
    std::array<double, 4> result{};
    for (unsigned c = 0; c != 4; ++c)
        for (unsigned k = 0; k != 4; ++k) result[c] += point[k] * m[k * 4 + c];
    return result;
}
Matrix Projective() {
    return {{.012f, .002f, .005f, .003f,
             .003f, -.009f, .002f, -.004f,
             -.002f, .003f, .014f, .001f,
             -1.f, .8f, 2.f, 1.2f}};
}
bool Near(const Matrix& a, const Matrix& b, float tolerance = .0002f) {
    for (unsigned i = 0; i != 16; ++i)
        if (std::abs(a[i] - b[i]) > tolerance * (1 + std::abs(b[i]))) return false;
    return true;
}
void Reset() {
    std::lock_guard<std::mutex> lock(ui_affine_clip::signatureLock);
    for (auto& item : ui_affine_clip::signatures) item = {};
    ui_affine_clip::nextSignature = 0;
    ui_affine_clip::geometryApplied = ui_affine_clip::textApplied = 0;
    ui_affine_clip::rejected = ui_affine_clip::disabled = 0;
    ui_affine_clip::unmatched = ui_affine_clip::cacheChanged = 0;
}
struct Rectangle {
    float values[4];
    uint8_t enabled;
    uint8_t padding[3];
};
static_assert(sizeof(Rectangle) == 20, "The real clip stack has 20-byte entries.");
struct Stack { uintptr_t data; uint32_t capacity, count; };
static_assert(sizeof(Stack) == 16, "The real clip stack descriptor has 16 bytes.");
struct Fixture {
    alignas(16) Bytes object{};
    std::array<Rectangle, 3> rectangles{};
    Fixture() {
        object.fill(0xa5);
        for (auto& rectangle : rectangles) {
            const float rect[4] = {40, 48, 176, 160};
            std::memcpy(rectangle.values, rect, sizeof(rect));
            rectangle.enabled = 1;
            // Deliberately nonzero: enabled is one byte, not an aligned uint32_t.
            rectangle.padding[0] = 0x5a; rectangle.padding[1] = 0x81; rectangle.padding[2] = 0xff;
        }
        const auto identity = Identity();
        Put(0x2a0, identity); Put(0x2e0, identity); Put(0x320, identity);
        Put(0x360, Projective()); Put(0x3a0, identity); Put(0x3e0, Projective());
        Stack stack{reinterpret_cast<uintptr_t>(rectangles.data()), 3, 3};
        Put(0x270, stack);
        Matrix cache{}, gpu{};
        for (unsigned i = 0; i != 16; ++i) { cache[i] = 1000.f + i; gpu[i] = -2000.f - i; }
        Put(0x470, cache); Put(0x5b0, gpu);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;
    template<class T> void Put(size_t offset, const T& value) {
        std::memcpy(object.data() + offset, &value, sizeof(value));
    }
    template<class T> T Get(size_t offset) const {
        T value{}; std::memcpy(&value, object.data() + offset, sizeof(value)); return value;
    }
    void Remember() {
        auto p = Get<Matrix>(0x360);
        ui_affine_clip::Remember(object.data(), p.data());
    }
};
bool UnchangedOutside(const Bytes& before, const Bytes& after, size_t first, size_t count) {
    for (size_t i = 0; i != before.size(); ++i)
        if ((i < first || i >= first + count) && before[i] != after[i]) return false;
    return true;
}

bool TestShaderContract() {
    const float rectangle[4] = {40, 48, 176, 160};
    auto local = Identity(); local[0] = .8f; local[5] = 1.1f; local[12] = 20; local[13] = -7;
    std::array<Matrix, 3> matrices{{Identity(), Projective(), Projective()}};
    matrices[2][3] = -.02f; matrices[2][7] = .03f; matrices[2][11] = .07f; matrices[2][15] = 1.6f;
    size_t oldContractFailures = 0;
    for (const auto& combined : matrices) {
        Matrix planes{}, oldPlanes{};
        const auto original = combined;
        REQUIRE(ui_affine_clip::ShaderPlanes(combined.data(), rectangle, planes.data()));
        REQUIRE(combined == original);
        REQUIRE(ui_clip_projection::RectPlanes(combined.data(), rectangle, oldPlanes.data()));
        for (double x : {-30., 40., 100., 216., 250.})
            for (double y : {10., 48., 120., 208., 240.})
                for (double z : {-3., 0., 5.})
                    for (bool useLocal : {false, true}) {
                        std::array<double, 4> source{{x, y, z, 1}};
                        if (useLocal) source = Point(source, local);
                        const auto transformed = Point(source, combined);
                        const double expected[4] = {source[1] - rectangle[1], source[0] - rectangle[0],
                            rectangle[1] + rectangle[3] - source[1], rectangle[0] + rectangle[2] - source[0]};
                        for (unsigned plane = 0; plane != 4; ++plane) {
                            double actual = -planes[plane * 4 + 3], old = -oldPlanes[plane * 4 + 3];
                            for (unsigned axis = 0; axis != 3; ++axis) {
                                actual += transformed[axis] * planes[plane * 4 + axis];
                                old += transformed[axis] * oldPlanes[plane * 4 + axis];
                            }
                            // Evaluate the real VS equation, with no divide and no multiplication by w.
                            REQUIRE(std::abs(actual - expected[plane]) < .0002 * (1 + std::abs(expected[plane])));
                            if (std::abs(old - expected[plane]) > .1) ++oldContractFailures;
                        }
                    }
    }
    REQUIRE(oldContractFailures > 500);
    Matrix p1{}, p2{};
    REQUIRE(ui_affine_clip::ShaderPlanes(matrices[1].data(), rectangle, p1.data()));
    REQUIRE(ui_affine_clip::ShaderPlanes(matrices[2].data(), rectangle, p2.data()));
    REQUIRE(p1 == p2); // Only w changed, which the real clipping equation never reads.
    std::printf("PASS real VS dp3-distance contract; old homogeneous negative controls=%zu\n", oldContractFailures);
    return true;
}

bool TestShaderRejections() {
    Matrix sentinel{}; sentinel.fill(1234.5f);
    const float rectangle[4] = {40, 48, 176, 160};
    for (unsigned index = 0; index != 16; ++index)
        for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            auto matrix = Projective(), output = sentinel; matrix[index] = invalid;
            REQUIRE(!ui_affine_clip::ShaderPlanes(matrix.data(), rectangle, output.data()));
            REQUIRE(output == sentinel);
        }
    for (unsigned index = 0; index != 4; ++index) {
        float bad[4]; std::memcpy(bad, rectangle, sizeof(bad));
        bad[index] = std::numeric_limits<float>::quiet_NaN();
        auto matrix = Projective(), output = sentinel;
        REQUIRE(!ui_affine_clip::ShaderPlanes(matrix.data(), bad, output.data()));
        REQUIRE(output == sentinel);
    }
    for (unsigned dimension : {2u, 3u}) {
        float bad[4]; std::memcpy(bad, rectangle, sizeof(bad)); bad[dimension] = -1;
        auto matrix = Projective(), output = sentinel;
        REQUIRE(!ui_affine_clip::ShaderPlanes(matrix.data(), bad, output.data()));
        REQUIRE(output == sentinel);
    }
    auto singular = Identity(); singular[10] = 0;
    auto output = sentinel;
    REQUIRE(!ui_affine_clip::ShaderPlanes(singular.data(), rectangle, output.data()));
    REQUIRE(output == sentinel);
    // Full 4x4 is invertible, but the xyz affine mapping is singular: reject it.
    singular[11] = singular[14] = 1; singular[15] = 0;
    REQUIRE(!ui_affine_clip::ShaderPlanes(singular.data(), rectangle, output.data()));
    REQUIRE(output == sentinel);
    std::puts("PASS singular/nonfinite/invalid rectangles reject without output writes");
    return true;
}

bool TestGeometryFootprint() {
    Reset(); Fixture fixture; fixture.Remember();
    const auto before = fixture.object;
    const auto rectangleBefore = fixture.rectangles;
    Matrix expected{}; auto combined = fixture.Get<Matrix>(0x3e0);
    REQUIRE(ui_affine_clip::ShaderPlanes(combined.data(), fixture.rectangles.back().values, expected.data()));
    ui_affine_clip::Prepared(fixture.object.data(), true);
    REQUIRE(Near(fixture.Get<Matrix>(0x5b0), expected));
    REQUIRE(UnchangedOutside(before, fixture.object, 0x5b0, 64));
    REQUIRE(std::memcmp(rectangleBefore.data(), fixture.rectangles.data(), sizeof(rectangleBefore)) == 0);
    REQUIRE(ui_affine_clip::geometryApplied == 1);
    REQUIRE(ui_affine_clip::rejected == 0);
    const auto applied = fixture.object;
    ui_affine_clip::Prepared(fixture.object.data(), false);
    REQUIRE(fixture.object == applied);
    REQUIRE(ui_affine_clip::geometryApplied == 1);
    std::puts("PASS geometry writes only +0x5b0..+0x5ef; nonzero enabled padding accepted");
    return true;
}

bool TestDisabledAndMismatch() {
    Reset(); Fixture fixture; fixture.Remember();
    fixture.rectangles.back().enabled = 0;
    const auto before = fixture.object;
    ui_affine_clip::Prepared(fixture.object.data(), true);
    { ui_affine_clip::TextScope scope(fixture.object.data(), true); REQUIRE(!scope.object); }
    REQUIRE(fixture.object == before);
    REQUIRE(ui_affine_clip::disabled == 2);
    REQUIRE(ui_affine_clip::geometryApplied == 0 && ui_affine_clip::textApplied == 0);
    fixture.rectangles.back().enabled = 1;
    auto projection = fixture.Get<Matrix>(0x360); projection[0] += .001f;
    fixture.Put(0x360, projection);
    const auto mismatch = fixture.object;
    ui_affine_clip::Prepared(fixture.object.data(), true);
    { ui_affine_clip::TextScope scope(fixture.object.data(), true); REQUIRE(!scope.object); }
    REQUIRE(fixture.object == mismatch);
    REQUIRE(ui_affine_clip::unmatched == 2);
    Fixture another;
    REQUIRE(!ui_affine_clip::Matches(another.object.data(), another.Get<Matrix>(0x360).data()));
    std::puts("PASS disabled clip and projection/object mismatch preserve all native bytes");
    return true;
}

bool TestComputeRejections() {
    for (unsigned condition = 0; condition != 17; ++condition) {
        Reset(); Fixture fixture; fixture.Remember();
        auto stack = fixture.Get<Stack>(0x270);
        if (condition == 0) stack.count = 0;
        if (condition == 1) stack.count = 4;
        if (condition == 2) { stack.count = 4097; stack.capacity = 5000; }
        if (condition == 3) stack.data = 0;
        if (condition == 4) stack.data = 1;
        if (condition == 16) stack.data = UINTPTR_MAX - 5; // Reject top-entry pointer arithmetic overflow.
        fixture.Put(0x270, stack);
        if (condition == 5) fixture.rectangles.back().enabled = 2;
        if (condition == 6) fixture.rectangles.back().values[2] = -1;
        if (condition == 7) fixture.rectangles.back().values[0] = std::numeric_limits<float>::infinity();
        if (condition == 8 || condition == 9) {
            auto bad = Identity(); bad[10] = condition == 8 ? 0 : std::numeric_limits<float>::quiet_NaN();
            fixture.Put(0x3e0, bad); fixture.Put(0x2e0, bad);
        }
        if (condition >= 10 && condition < 16) {
            auto local = Identity();
            if (condition < 14) local[(condition - 10) * 4 + 3] += .01f;
            if (condition == 14) local[0] = std::numeric_limits<float>::quiet_NaN();
            if (condition == 15) local[12] = std::numeric_limits<float>::infinity();
            fixture.Put(0x320, local);
        }
        const auto before = fixture.object;
        ui_affine_clip::Prepared(fixture.object.data(), true);
        { ui_affine_clip::TextScope scope(fixture.object.data(), true); REQUIRE(!scope.object); }
        REQUIRE(fixture.object == before);
        REQUIRE(ui_affine_clip::geometryApplied == 0 && ui_affine_clip::textApplied == 0);
        REQUIRE(ui_affine_clip::rejected == 2);
    }
    std::puts("PASS invalid stack/clip/cache and projective/nonfinite local transforms reject unchanged");
    return true;
}

bool TestCrossThreadSignatures() {
    Reset();
    std::array<std::array<unsigned char, 16>, 8> tokens{};
    std::array<Matrix, 8> projections{};
    for (unsigned i = 0; i != projections.size(); ++i) {
        projections[i] = Projective(); projections[i][12] += float(i);
    }
    std::thread producer([&] {
        for (unsigned i = 0; i != projections.size(); ++i)
            ui_affine_clip::Remember(tokens[i].data(), projections[i].data());
    });
    producer.join();
    std::atomic<unsigned> failures{0};
    std::vector<std::thread> readers;
    for (unsigned thread = 0; thread != 4; ++thread) readers.emplace_back([&, thread] {
        for (unsigned iteration = 0; iteration != 10000; ++iteration) {
            const unsigned own = thread, other = 4 + thread;
            ui_affine_clip::Remember(tokens[own].data(), projections[own].data());
            if (!ui_affine_clip::Matches(tokens[other].data(), projections[other].data())) ++failures;
            if (ui_affine_clip::Matches(tokens[other].data(), projections[own].data())) ++failures;
        }
    });
    for (auto& reader : readers) reader.join();
    REQUIRE(failures == 0);
    REQUIRE(ui_affine_clip::nextSignature == 8);
    std::puts("PASS cross-thread Remember/Matches: 40,000 concurrent iterations, no TLS dependency");
    return true;
}

bool TestTextScopeRestore() {
    Reset(); Fixture fixture;
    auto a = Identity(), b = Identity();
    a[0] = .9f; a[5] = 1.2f; a[12] = -.1f; b[0] = .7f; b[5] = 1.1f; b[12] = 8;
    fixture.Put(0x2a0, a); fixture.Put(0x2e0, b); fixture.Put(0x3e0, Identity());
    fixture.Remember();
    const auto before = fixture.object;
    const auto combined = Product(Product(b, fixture.Get<Matrix>(0x360)), a);
    Matrix expected{};
    REQUIRE(ui_affine_clip::ShaderPlanes(combined.data(), fixture.rectangles.back().values, expected.data()));
    {
        ui_affine_clip::TextScope scope(fixture.object.data(), true);
        REQUIRE(scope.object == fixture.object.data());
        REQUIRE(Near(fixture.Get<Matrix>(0x470), expected));
        REQUIRE(UnchangedOutside(before, fixture.object, 0x470, 64));
        REQUIRE(fixture.Get<Matrix>(0x3e0) == Identity()); // Dirty combined cache is not trusted or changed.
    }
    REQUIRE(fixture.object == before);
    REQUIRE(ui_affine_clip::textApplied == 1 && ui_affine_clip::cacheChanged == 0);
    { ui_affine_clip::TextScope inactive(fixture.object.data(), false); REQUIRE(!inactive.object); }
    REQUIRE(fixture.object == before);
    try {
        ui_affine_clip::TextScope scope(fixture.object.data(), true);
        throw 7;
    } catch (int value) { REQUIRE(value == 7); }
    REQUIRE(fixture.object == before);
    REQUIRE(ui_affine_clip::textApplied == 2 && ui_affine_clip::cacheChanged == 0);
    std::puts("PASS text recomputes B*P*A; temporary cache-only writes restore normally and on unwind");
    return true;
}

bool TestTextScopeNested() {
    Reset(); Fixture fixture; fixture.Remember();
    const auto before = fixture.object;
    {
        ui_affine_clip::TextScope outer(fixture.object.data(), true);
        REQUIRE(outer.object);
        const auto outerCache = fixture.Get<Matrix>(0x470);
        {
            ui_affine_clip::TextScope same(fixture.object.data(), true);
            REQUIRE(same.object);
            REQUIRE(fixture.Get<Matrix>(0x470) == outerCache);
        }
        REQUIRE(fixture.Get<Matrix>(0x470) == outerCache);
        fixture.rectangles.back().values[0] += 17;
        {
            ui_affine_clip::TextScope different(fixture.object.data(), true);
            REQUIRE(different.object);
            REQUIRE(fixture.Get<Matrix>(0x470) != outerCache);
        }
        REQUIRE(fixture.Get<Matrix>(0x470) == outerCache);
    }
    REQUIRE(fixture.object == before);
    REQUIRE(ui_affine_clip::textApplied == 3 && ui_affine_clip::cacheChanged == 0);
    std::puts("PASS identical and distinct nested text scopes restore in LIFO order");
    return true;
}

bool TestTextScopeInternalRefresh() {
    Reset(); Fixture fixture; fixture.Remember();
    Matrix legitimate{}; for (unsigned i = 0; i != 16; ++i) legitimate[i] = 3000.f + i;
    const auto before = fixture.object;
    {
        ui_affine_clip::TextScope outer(fixture.object.data(), true);
        REQUIRE(outer.object);
        fixture.rectangles.back().values[1] += 23;
        {
            ui_affine_clip::TextScope inner(fixture.object.data(), true);
            REQUIRE(inner.object);
            fixture.Put(0x470, legitimate); // Simulate a native clip stack/cache refresh inside the call.
        }
        REQUIRE(fixture.Get<Matrix>(0x470) == legitimate);
    }
    REQUIRE(fixture.Get<Matrix>(0x470) == legitimate);
    REQUIRE(UnchangedOutside(before, fixture.object, 0x470, 64));
    REQUIRE(ui_affine_clip::cacheChanged == 2);
    std::puts("PASS internal native cache refresh survives both nested destructors");
    return true;
}
} // namespace

int main() {
    const bool okay = TestShaderContract() && TestShaderRejections() && TestGeometryFootprint() &&
        TestDisabledAndMismatch() && TestComputeRejections() && TestCrossThreadSignatures() &&
        TestTextScopeRestore() && TestTextScopeNested() && TestTextScopeInternalRefresh();
    std::printf("%s: ui_affine_clip CPU tests, %llu checks\n", okay ? "PASS" : "FAIL", checks);
    return okay ? 0 : 1;
}
