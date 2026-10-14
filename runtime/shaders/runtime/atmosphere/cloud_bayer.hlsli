// @ref Bayer 1973 "An Optimum Method for Two-Level Rendition of Continuous-Tone Pictures"
// @constants 4x4 ordered pattern, sixteen temporal slots.
static const uint bayerMatrix[4][4] = {
    { 0, 8, 2, 10 },
    { 12, 4, 14, 6 },
    { 3, 11, 1, 9 },
    { 15, 7, 13, 5 }
};

uint2 CloudBayerOffset(uint frameIndex) {
    uint2 offset = 0;
    [unroll]
    for (uint x = 0; x < 4; ++x) {
        [unroll]
        for (uint y = 0; y < 4; ++y) {
            if (bayerMatrix[x][y] == frameIndex % 16) offset = uint2(x, y);
        }
    }
    return offset;
}
