// EXCALIBUR369 - secp256k1 group operations.
// Affine + Jacobian points, batch (Montgomery) inversion, windowed kG,
// and the incremental batched step used by the hot loop.
#pragma once
#include <vector>
#include "fe.h"
#include "u256.h"

namespace ex369 {

struct PointA {           // affine; infinity flag kept explicit
    Fe x, y;
    bool inf = false;
};

struct PointJ {           // jacobian: (X:Y:Z) -> (X/Z^2, Y/Z^3); Z==0 is infinity
    Fe X, Y, Z;
    bool is_inf() const { return Z.is_zero(); }
    static PointJ infinity() { PointJ p; p.X = Fe(1); p.Y = Fe(1); p.Z = Fe(0); return p; }
};

// Group order n.
static const uint64_t CURVE_N[4] = {
    0xBFD25E8CD0364141ULL, 0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFFULL
};

const PointA& generator();

// ---- basic ops -------------------------------------------------------------

// NOTE: r and p may alias (the batched lane chain calls add(acc, acc, D), which
// lands here on an x-collision). Z3 therefore has to be computed from p.Y and
// p.Z BEFORE r.Y is written, or an aliased call would silently use the new Y.
inline void point_j_double(PointJ& r, const PointJ& p) {
    if (p.is_inf()) { r = PointJ::infinity(); return; }
    // dbl-2009-l (a=0)
    Fe A, B, C, D, t1, t2, Z3;
    fe_mul(Z3, p.Y, p.Z);              // computed up-front: r may alias p
    fe_add(Z3, Z3, Z3);                // Z3 = 2*Y*Z
    fe_sqr(A, p.X);                    // A = X^2
    fe_sqr(B, p.Y);                    // B = Y^2
    fe_sqr(C, B);                      // C = B^2
    fe_add(t1, p.X, B);                // X+B
    fe_sqr(t1, t1);                    // (X+B)^2
    fe_sub(t1, t1, A);
    fe_sub(t1, t1, C);
    fe_add(D, t1, t1);                 // D = 2*((X+B)^2 - A - C)
    fe_add(t2, A, A);
    fe_add(t2, t2, A);                 // E = 3A
    fe_sqr(t1, t2);                    // F = E^2
    fe_add(r.X, D, D);
    fe_sub(r.X, t1, r.X);              // X3 = F - 2D
    fe_sub(t1, D, r.X);
    fe_mul(t1, t2, t1);                // E*(D - X3)
    fe_add(C, C, C); fe_add(C, C, C); fe_add(C, C, C);  // 8C
    fe_sub(r.Y, t1, C);
    r.Z = Z3;
}

// Mixed addition: jacobian + affine.
inline void point_j_add_affine(PointJ& r, const PointJ& p, const PointA& q) {
    if (q.inf) { r = p; return; }
    if (p.is_inf()) { r.X = q.x; r.Y = q.y; r.Z = Fe(1); return; }
    Fe Z1Z1, U2, S2, H, HH, I, J, rr, V, t;
    fe_sqr(Z1Z1, p.Z);
    fe_mul(U2, q.x, Z1Z1);
    fe_mul(S2, q.y, Z1Z1);
    fe_mul(S2, S2, p.Z);
    fe_sub(H, U2, p.X);
    fe_sub(rr, S2, p.Y);
    if (H.is_zero()) {
        if (rr.is_zero()) { point_j_double(r, p); return; }
        r = PointJ::infinity();
        return;
    }
    fe_add(rr, rr, rr);
    fe_sqr(HH, H);
    fe_add(I, HH, HH); fe_add(I, I, I);       // I = 4*HH
    fe_mul(J, H, I);
    fe_mul(V, p.X, I);
    fe_sqr(t, rr);
    fe_sub(t, t, J);
    fe_add(r.X, V, V);
    fe_sub(r.X, t, r.X);
    fe_sub(t, V, r.X);
    fe_mul(t, rr, t);
    fe_mul(I, p.Y, J);
    fe_add(I, I, I);
    fe_sub(r.Y, t, I);
    fe_mul(r.Z, p.Z, H);
    fe_add(r.Z, r.Z, r.Z);
}

inline void point_j_to_affine(PointA& r, const PointJ& p) {
    if (p.is_inf()) { r.inf = true; r.x = Fe(); r.y = Fe(); return; }
    Fe zi, zi2, zi3;
    fe_inv(zi, p.Z);
    fe_sqr(zi2, zi);
    fe_mul(zi3, zi2, zi);
    fe_mul(r.x, p.X, zi2);
    fe_mul(r.y, p.Y, zi3);
    r.inf = false;
}

// Full affine addition (used for edge cases and by the independent verifier).
inline void point_a_add(PointA& r, const PointA& p, const PointA& q) {
    if (p.inf) { r = q; return; }
    if (q.inf) { r = p; return; }
    if (fe_eq(p.x, q.x)) {
        Fe negy; fe_neg(negy, q.y);
        if (fe_eq(p.y, negy)) { r.inf = true; return; }
        // doubling: lambda = 3x^2 / 2y
        Fe num, den, lam, t;
        fe_sqr(num, p.x);
        fe_add(t, num, num); fe_add(num, t, num);
        fe_add(den, p.y, p.y);
        fe_inv(den, den);
        fe_mul(lam, num, den);
        fe_sqr(t, lam);
        fe_sub(t, t, p.x); fe_sub(t, t, q.x);
        Fe rx = t;
        fe_sub(t, p.x, rx);
        fe_mul(t, lam, t);
        fe_sub(r.y, t, p.y);
        r.x = rx;
        r.inf = false;
        return;
    }
    Fe num, den, lam, t;
    fe_sub(num, q.y, p.y);
    fe_sub(den, q.x, p.x);
    fe_inv(den, den);
    fe_mul(lam, num, den);
    fe_sqr(t, lam);
    fe_sub(t, t, p.x); fe_sub(t, t, q.x);
    Fe rx = t;
    fe_sub(t, p.x, rx);
    fe_mul(t, lam, t);
    fe_sub(r.y, t, p.y);
    r.x = rx;
    r.inf = false;
}

inline void point_a_neg(PointA& r, const PointA& p) {
    r.x = p.x; r.inf = p.inf;
    fe_neg(r.y, p.y);
}

// ---- batch inversion -------------------------------------------------------

// Montgomery batch inversion: one fe_inv for the whole array.
//
// `out` doubles as the scratch space for the running prefix products, so no
// third array is needed. A zero input yields a zero output; the caller handles
// those lanes directly (in the EC step, a zero dx means an x-coordinate
// collision, i.e. a doubling or the point at infinity).
inline void batch_inverse(Fe* out, const Fe* in, size_t n) {
    if (n == 0) return;
    Fe acc(1);
    for (size_t i = 0; i < n; i++) {
        out[i] = acc;                                  // prefix product before in[i]
        if (!in[i].is_zero()) fe_mul(acc, acc, in[i]);
    }
    Fe u;
    fe_inv(u, acc);                                    // 1 / product of all nonzero in[]
    for (size_t i = n; i-- > 0;) {
        if (in[i].is_zero()) { out[i] = Fe(); continue; }
        Fe t;
        fe_mul(t, out[i], u);                          // = 1 / in[i]
        fe_mul(u, u, in[i]);                           // strip in[i] from the product
        out[i] = t;
    }
}

// ---- scalar multiplication -------------------------------------------------

// Windowed (w=4) fixed-base table over G, built once at startup.
struct GenTable {
    static constexpr int W = 4;
    static constexpr int WINDOWS = 64;      // 256 / 4
    static constexpr int ENTRIES = 15;      // 1..15 multiples
    PointA tbl[WINDOWS][ENTRIES];
    void build();
};
const GenTable& gen_table();

// r = k * G  (k reduced mod nothing; k == 0 yields infinity)
void ec_mul_gen(PointA& r, const U256& k);

// r = k * P, generic (used by tests / verifier only, not the hot loop)
void ec_mul_point(PointA& r, const PointA& p, const U256& k);

// Compressed SEC serialization -> 33 bytes.
inline void serialize_compressed(const PointA& p, uint8_t out[33]) {
    out[0] = p.y.is_odd() ? 0x03 : 0x02;
    fe_to_be32(p.x, out + 1);
}

} // namespace ex369
