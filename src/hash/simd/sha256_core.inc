// SHA-256 multi-buffer core. Included once per ISA with VEC/VADD/... defined.
// Input:  w[64] schedule already transposed (w[i] holds word i of every lane).
// Output: st[8] chaining values, one vector per state word.
{
    static const uint32_t KC[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

    for (int i = 16; i < 64; i++)
        w[i] = VADD(VADD(w[i-16], V_s0(w[i-15])), VADD(w[i-7], V_s1(w[i-2])));

    VEC a = VSET1(0x6a09e667), b = VSET1(0xbb67ae85);
    VEC c = VSET1(0x3c6ef372), d = VSET1(0xa54ff53a);
    VEC e = VSET1(0x510e527f), f = VSET1(0x9b05688c);
    VEC g = VSET1(0x1f83d9ab), h = VSET1(0x5be0cd19);

    for (int i = 0; i < 64; i++) {
        VEC t1 = VADD(VADD(h, V_S1(e)), VADD(V_CH(e, f, g), VADD(VSET1(KC[i]), w[i])));
        VEC t2 = VADD(V_S0(a), V_MAJ(a, b, c));
        h = g; g = f; f = e; e = VADD(d, t1);
        d = c; c = b; b = a; a = VADD(t1, t2);
    }
    st[0] = VADD(VSET1(0x6a09e667), a);
    st[1] = VADD(VSET1(0xbb67ae85), b);
    st[2] = VADD(VSET1(0x3c6ef372), c);
    st[3] = VADD(VSET1(0xa54ff53a), d);
    st[4] = VADD(VSET1(0x510e527f), e);
    st[5] = VADD(VSET1(0x9b05688c), f);
    st[6] = VADD(VSET1(0x1f83d9ab), g);
    st[7] = VADD(VSET1(0x5be0cd19), h);
}
