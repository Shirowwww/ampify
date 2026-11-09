#include "test.hpp"

#include "../src/core/theme/properties.hpp"
#include "../src/core/theme/value_codec.hpp"

using namespace ampify::theme;

TEST_CASE(ValueCodec_ImageKeepsItsPathAndFit) {
    CHECK(EncodeImage({"C:/themes/glass/bg.png", ImageFit::Default}) == "C:/themes/glass/bg.png");
    CHECK(EncodeImage({"C:/themes/glass/bg.png", ImageFit::UniformToFill}) == "C:/themes/glass/bg.png|UniformToFill");
    CHECK(EncodeImage({"artwork:", ImageFit::Uniform}) == "artwork:|Uniform");
    for (ImageFit fit : {ImageFit::Default, ImageFit::Fill, ImageFit::Uniform, ImageFit::UniformToFill}) {
        ImageRef image{"D:/a b/c.gif", fit};
        CHECK(DecodeImage(EncodeImage(image)) == image);
    }
    CHECK(DecodeImage("x.png|Tile").fit == ImageFit::Default);  // an unknown fit leaves the element's own
    CHECK(DecodeImage("x.png|Tile").path == "x.png");
}

TEST_CASE(ValueCodec_CornersKeepTheEngineOrder) {
    // corner-radius: 1 2 3 4 (top-left, top-right, bottom-right, bottom-left) reaches the backend as bottom-left first.
    Corners corners{1, 2, 3, 4};
    CHECK(EncodeCorners(corners) == "4,1,2,3");
    REQUIRE(DecodeCorners("4,1,2,3").has_value());
    CHECK(*DecodeCorners("4,1,2,3") == corners);
    CHECK(CornersInCssOrder("4,1,2,3") == std::optional<std::string>("1,2,3,4"));
    CHECK(!DecodeCorners("1,2,3").has_value());
    CHECK(!DecodeCorners("1,2,x,4").has_value());
    CHECK(!CornersInCssOrder("8").has_value());
    CHECK(ShownText("CornerRadius", Value{ValueKind::Thickness, "4,1,2,3"}) == "1,2,3,4");
    CHECK(ShownText("Margin", Value{ValueKind::Thickness, "4,1,2,3"}) == "4,1,2,3");
}

TEST_CASE(ValueCodec_HandleCarriesItsNumberAndColour) {
    CHECK(EncodeHandle(42) == "handle:42");
    CHECK(DecodeHandle("handle:42") == std::optional<std::uint64_t>(42));
    CHECK(DecodeHandle("handle:42;#80FFFFFF") == std::optional<std::uint64_t>(42));
    CHECK(HandleColour("handle:42;#80FFFFFF") == "#80FFFFFF");
    CHECK(HandleColour("handle:42").empty());
    CHECK(!DecodeHandle("no-own-value").has_value());
    CHECK(!DecodeHandle("handle:").has_value());
}

TEST_CASE(ValueCodec_VisibilityIsExactlyTwoWords) {
    CHECK(DecodeVisible(kVisible) == std::optional<bool>(true));
    CHECK(DecodeVisible(kCollapsed) == std::optional<bool>(false));
    CHECK(!DecodeVisible("collapsed").has_value());
    CHECK(!DecodeVisible("").has_value());
}
