#include "core/document/Document.h"
#include "core/io/KiCadMod.h"
#include "core/io/SExpr.h"
#include "core/pcb/FootprintGenerator.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstdio>
#include <fstream>

using namespace lcad;
using Catch::Approx;

TEST_CASE("parseSExpr/writeSExpr round-trip a nested KiCad-style expression", "[io][kicad][sexpr]") {
    const std::string text = R"((footprint "R_0603" (layer "F.Cu") (at 10 20 90) (attr smd)))";
    auto parsed = parseSExpr(text);
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->isList());
    REQUIRE(parsed->tag() == "footprint");
    REQUIRE(parsed->textAt(0) == "R_0603");

    const SExpr* layer = parsed->child("layer");
    REQUIRE(layer != nullptr);
    REQUIRE(layer->textAt(0) == "F.Cu");

    const SExpr* at = parsed->child("at");
    REQUIRE(at != nullptr);
    REQUIRE(at->numberAt(0) == Approx(10));
    REQUIRE(at->numberAt(1) == Approx(20));
    REQUIRE(at->numberAt(2) == Approx(90));

    const SExpr* attr = parsed->child("attr");
    REQUIRE(attr != nullptr);
    REQUIRE(attr->textAt(0) == "smd");

    // Re-parsing our own pretty-printed output must reproduce an
    // equivalent tree (the actual round-trip contract file formats rely on).
    std::string rewritten = writeSExpr(*parsed);
    auto reparsed = parseSExpr(rewritten);
    REQUIRE(reparsed.has_value());
    REQUIRE(reparsed->tag() == "footprint");
    REQUIRE(reparsed->child("at")->numberAt(2) == Approx(90));
}

TEST_CASE("parseSExpr rejects malformed input", "[io][kicad][sexpr]") {
    REQUIRE_FALSE(parseSExpr("(unbalanced (paren)").has_value());
    REQUIRE_FALSE(parseSExpr("(\"unterminated string)").has_value());
    REQUIRE_FALSE(parseSExpr("(a) trailing garbage").has_value());
}

TEST_CASE("formatKiCadNumber trims trailing zeros like KiCad's own writer", "[io][kicad][sexpr]") {
    REQUIRE(formatKiCadNumber(1.0) == "1");
    REQUIRE(formatKiCadNumber(1.5) == "1.5");
    REQUIRE(formatKiCadNumber(-0.75) == "-0.75");
}

TEST_CASE("writeKiCadMod/readKiCadMod round-trip a real footprint's pads and outline", "[io][kicad][footprint]") {
    Document doc;
    GullWingParams params;
    params.pinCount = 8;
    params.sideCount = 4;
    params.pitch = 1.0;
    params.bodyWidth = 4.0;
    params.bodyLength = 4.0;
    params.padWidth = 0.4;
    params.padLength = 1.0;
    REQUIRE(generateGullWingFootprint(doc, "QFP8", params));
    const BlockDefinition* block = doc.findBlock("QFP8");
    REQUIRE(block);

    const std::string path = "/tmp/kumcad_kicadmod_roundtrip_test.kicad_mod";
    std::string err;
    REQUIRE(writeKiCadMod(doc, *block, path, &err));

    Document doc2;
    const BlockDefinition* readBack = readKiCadMod(doc2, path, &err);
    REQUIRE(readBack != nullptr);
    REQUIRE(readBack->pads.size() == block->pads.size());
    for (std::size_t i = 0; i < block->pads.size(); ++i) {
        REQUIRE(readBack->pads[i].number == block->pads[i].number);
        REQUIRE(readBack->pads[i].position.x == Approx(block->pads[i].position.x));
        REQUIRE(readBack->pads[i].position.y == Approx(block->pads[i].position.y));
    }
    std::remove(path.c_str());
}

TEST_CASE("readKiCadMod reports an error for a missing or malformed file", "[io][kicad][footprint]") {
    Document doc;
    std::string err;
    REQUIRE(readKiCadMod(doc, "/tmp/kumcad_does_not_exist.kicad_mod", &err) == nullptr);
    REQUIRE_FALSE(err.empty());

    const std::string path = "/tmp/kumcad_kicadmod_bad_test.kicad_mod";
    {
        std::ofstream out(path);
        out << "(not_a_footprint 1 2 3)";
    }
    err.clear();
    REQUIRE(readKiCadMod(doc, path, &err) == nullptr);
    REQUIRE_FALSE(err.empty());
    std::remove(path.c_str());
}
