#include "group.h"
#include <mutex>

namespace ex369 {

// G in big-endian bytes, so the generator is independent of how Fe stores limbs.
static const uint8_t GX[32] = {
    0x79,0xBE,0x66,0x7E,0xF9,0xDC,0xBB,0xAC, 0x55,0xA0,0x62,0x95,0xCE,0x87,0x0B,0x07,
    0x02,0x9B,0xFC,0xDB,0x2D,0xCE,0x28,0xD9, 0x59,0xF2,0x81,0x5B,0x16,0xF8,0x17,0x98
};
static const uint8_t GY[32] = {
    0x48,0x3A,0xDA,0x77,0x26,0xA3,0xC4,0x65, 0x5D,0xA4,0xFB,0xFC,0x0E,0x11,0x08,0xA8,
    0xFD,0x17,0xB4,0x48,0xA6,0x85,0x54,0x19, 0x9C,0x47,0xD0,0x8F,0xFB,0x10,0xD4,0xB8
};

const PointA& generator() {
    static PointA g = [] {
        PointA p;
        p.x = fe_from_be32(GX);
        p.y = fe_from_be32(GY);
        p.inf = false;
        return p;
    }();
    return g;
}

void GenTable::build() {
    PointA base = generator();
    for (int w = 0; w < WINDOWS; w++) {
        PointA acc = base;
        tbl[w][0] = acc;
        for (int j = 1; j < ENTRIES; j++) {
            PointA next;
            point_a_add(next, acc, base);
            tbl[w][j] = next;
            acc = next;
        }
        // base <<= 4  (base = 16 * base)
        for (int i = 0; i < W; i++) {
            PointA d;
            point_a_add(d, base, base);
            base = d;
        }
    }
}

const GenTable& gen_table() {
    static GenTable* t = [] {
        GenTable* g = new GenTable();
        g->build();
        return g;
    }();
    return *t;
}

void ec_mul_gen(PointA& r, const U256& k) {
    const GenTable& t = gen_table();
    PointJ acc = PointJ::infinity();
    for (int w = 0; w < GenTable::WINDOWS; w++) {
        unsigned shift = w * 4;
        unsigned limb = shift >> 6, off = shift & 63;
        unsigned digit = (unsigned)((k.d[limb] >> off) & 0xF);
        if (digit) point_j_add_affine(acc, acc, t.tbl[w][digit - 1]);
    }
    point_j_to_affine(r, acc);
}

void ec_mul_point(PointA& r, const PointA& p, const U256& k) {
    PointJ acc = PointJ::infinity();
    PointA cur = p;
    for (int i = 0; i < 256; i++) {
        if (k.bit(i)) point_j_add_affine(acc, acc, cur);
        PointA d;
        point_a_add(d, cur, cur);
        cur = d;
    }
    point_j_to_affine(r, acc);
}

} // namespace ex369
