#include "PixelReadback.h"
#include <iostream>

int main() try {
    constexpr int width = 20, height = 20;
    const PixelReadback::Region region{5,5,15,15};
    const std::vector<uint8_t> blank(width*height*4, 0);
    auto effect = blank;
    const auto paint = [width](auto& pixels, int x, int y, uint8_t value) {
        pixels[size_t(y*width+x)*4] = value;
    };
    for (int y = 7; y < 11; ++y) for (int x = 7; x < 11; ++x) paint(effect,x,y,255);
    const auto check = [&](const auto& control, const auto& repeat, const auto& image, bool accepted) {
        bool passed = true;
        try { PixelReadback::requireEffect(control,repeat,image,width,height,region,"mutation"); }
        catch (const std::runtime_error&) { passed = false; }
        if (passed != accepted) throw std::runtime_error("readback mutation guard returned the wrong verdict");
    };
    check(blank,blank,effect,true);
    check(blank,blank,blank,false); // Missing draw.
    auto stray = blank; paint(stray,8,8,255);
    check(blank,blank,stray,false); // One stray pixel is insufficient.
    stray = effect; paint(stray,1,1,255);
    check(blank,blank,stray,false); // Unrelated content cannot prove this effect.
    check(blank,stray,effect,false); // A moving background is not a control.
    check(blank,blank,std::vector<uint8_t>{},false);
    stray = effect; paint(stray,1,1,3);
    check(blank,blank,stray,true); // Rounding below tolerance is ignored.
    const auto ui = PixelReadback::uiRegion(3,4,5,6,height);
    if (ui.left != 3 || ui.top != 10 || ui.right != 8 || ui.bottom != 16)
        throw std::runtime_error("UI/readback origin conversion failed");
    std::cout << "Pixel readback negative controls passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
