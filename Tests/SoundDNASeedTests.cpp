#include <JuceHeader.h>
#include "../Source/Engine/SoundDNA.h"
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
int fail(const char* message) {
    std::cerr << "SONARA seed-state test failure: " << message << '\n';
    return 1;
}
}

int main() {
    sonara::SoundDNA dna;

    dna.seed = std::numeric_limits<uint64_t>::max();
    const auto maxState = dna.toValueTree();
    if (sonara::SoundDNA::fromValueTree(maxState).seed != std::numeric_limits<uint64_t>::max())
        return fail("UINT64_MAX did not round-trip");

    dna.seed = 0xFEDCBA9876543210ULL;
    const auto highBitState = dna.toValueTree();
    if (sonara::SoundDNA::fromValueTree(highBitState).seed != 0xFEDCBA9876543210ULL)
        return fail("high-bit seed did not round-trip");

    auto malformed = highBitState.createCopy();
    malformed.setProperty("seed", "18446744073709551616", nullptr);
    if (sonara::SoundDNA::fromValueTree(malformed).seed != 1ULL)
        return fail("overflowing seed was accepted");

    malformed.setProperty("seed", "1234x", nullptr);
    if (sonara::SoundDNA::fromValueTree(malformed).seed != 1ULL)
        return fail("non-decimal seed was accepted");

    malformed.setProperty("seed", "", nullptr);
    if (sonara::SoundDNA::fromValueTree(malformed).seed != 1ULL)
        return fail("empty seed did not use safe fallback");

    std::cout << "SONARA full-width seed/state tests passed\n";
    return 0;
}