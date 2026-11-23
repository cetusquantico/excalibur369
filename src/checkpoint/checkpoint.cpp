#include "checkpoint.h"
#include "../hash/sha256.h"
#include "../verify/verify.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <limits>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace ex369 {

static std::string jstr(const std::string& s) {
    std::string o = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(c); }
        else if (c == '\n') o += "\\n";
        else o.push_back(c);
    }
    o.push_back('"');
    return o;
}

std::string Checkpoint::payload() const {
    std::ostringstream f;
    f.imbue(std::locale::classic());
    f << std::setprecision(std::numeric_limits<double>::max_digits10);
    f << "{\n";
    f << "  \"version\": " << version << ",\n";
    f << "  \"tool\": " << jstr(tool) << ",\n";
    f << "  \"puzzle\": " << puzzle << ",\n";
    f << "  \"range_lo\": " << jstr(range_lo) << ",\n";
    f << "  \"range_hi\": " << jstr(range_hi) << ",\n";
    f << "  \"target_hash160\": " << jstr(target_hash160) << ",\n";
    f << "  \"global_order\": " << jstr(global_order) << ",\n";
    f << "  \"local_direction\": " << jstr(local_direction) << ",\n";
    f << "  \"seed\": " << seed << ",\n";
    f << "  \"fixed_enabled\": " << (fixed_enabled ? "true" : "false") << ",\n";
    f << "  \"fixed_mask\": " << jstr(fixed_mask) << ",\n";
    f << "  \"fixed_value\": " << jstr(fixed_value) << ",\n";
    f << "  \"fixed_rotation\": " << jstr(fixed_rotation) << ",\n";
    f << "  \"fixed_positions\": [";
    for (size_t i = 0; i < fixed_positions.size(); i++) {
        if (i) f << ",";
        f << fixed_positions[i];
    }
    f << "],\n";
    f << "  \"round_robin\": " << (round_robin ? "true" : "false") << ",\n";
    f << "  \"slice_keys\": " << jstr(slice_keys) << ",\n";
    f << "  \"segment_size\": " << jstr(segment_size) << ",\n";
    f << "  \"config_hash\": " << jstr(config_hash) << ",\n";
    f << "  \"watermark\": " << watermark << ",\n";
    f << "  \"done_above\": [";
    for (size_t i = 0; i < done_above.size(); i++) {
        if (i) f << ",";
        f << done_above[i];
    }
    f << "],\n";
    f << "  \"current_pattern\": " << current_pattern << ",\n";
    f << "  \"current_segment\": " << jstr(current_segment) << ",\n";
    f << "  \"local_offset\": " << jstr(local_offset) << ",\n";
    f << "  \"total_candidates\": " << jstr(total_candidates) << ",\n";
    f << "  \"elapsed_seconds\": " << elapsed_seconds << "\n";
    f << "}\n";
    return f.str();
}

bool Checkpoint::save(const std::string& path, std::string& err) const {
    std::string data = payload();
    if (version == 2) {
        uint8_t hash[32];
        sha256((const uint8_t*)data.data(),data.size(),hash);
        data.insert(data.rfind("\n}"), ",\n  \"integrity_hash\": \"" + to_hex(hash,32) + "\"");
    }
    // A unique file in the same directory makes rename atomic and avoids
    // collisions between saves. Flush both the data and directory entry.
    std::string tmp = path + ".tmp.XXXXXX";
    int fd = mkstemp(tmp.data());
    if (fd < 0) { err = "cannot create checkpoint temporary file: " + std::string(strerror(errno)); return false; }
    bool ok = true;
    for (size_t off = 0; off < data.size();) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        off += (size_t)n;
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (!ok) { err = "checkpoint write/fsync failed"; unlink(tmp.c_str()); return false; }
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        err = "checkpoint rename failed: " + std::string(strerror(errno));
        unlink(tmp.c_str()); return false;
    }
    size_t slash = path.find_last_of('/');
    std::string dir = slash == std::string::npos ? "." : (slash == 0 ? "/" : path.substr(0, slash));
    int dfd = open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd < 0) { err = "cannot open checkpoint directory for fsync"; return false; }
    ok = fsync(dfd) == 0;
    if (close(dfd) != 0) ok = false;
    if (!ok) { err = "checkpoint directory fsync failed"; return false; }
    return true;
}

namespace {
// Strict parser for the checkpoint schema: a flat object containing strings,
// numbers, booleans and arrays of unsigned integers. No substring extraction.
struct JsonValue { char type = 0; std::string text; std::vector<uint64_t> array; };
bool uint_value(const std::string& text, uint64_t& out) {
    if (text.empty()) return false;
    auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    return r.ec == std::errc() && r.ptr == text.data() + text.size();
}
class Reader {
    const std::string& s;
    size_t p = 0;
    void ws() { while (p < s.size() && (s[p]==' ' || s[p]=='\n' || s[p]=='\r' || s[p]=='\t')) p++; }
    bool take(char c) { ws(); if (p == s.size() || s[p] != c) return false; p++; return true; }
    bool str(std::string& out) {
        if (!take('"')) return false;
        while (p < s.size()) {
            unsigned char c = s[p++];
            if (c == '"') return true;
            if (c < 32) return false;
            if (c == '\\') {
                if (p == s.size()) return false;
                c = s[p++];
                switch (c) {
                    case '"': case '\\': case '/': break;
                    case 'b': c='\b'; break; case 'f': c='\f'; break;
                    case 'n': c='\n'; break; case 'r': c='\r'; break; case 't': c='\t'; break;
                    case 'u': {
                        // All strings in this schema are ASCII identifiers or hex.
                        if (p + 4 > s.size()) return false;
                        unsigned code = 0;
                        for (int i=0; i<4; i++) {
                            char h=s[p++]; unsigned d;
                            if (h>='0' && h<='9') d=h-'0';
                            else if (h>='a' && h<='f') d=h-'a'+10;
                            else if (h>='A' && h<='F') d=h-'A'+10;
                            else return false;
                            code=code*16+d;
                        }
                        if (code>127) return false;
                        c=(unsigned char)code; break;
                    }
                    default: return false;
                }
            }
            out.push_back((char)c);
        }
        return false;
    }
    bool number(std::string& out) {
        ws(); size_t start=p;
        if (p<s.size() && s[p]=='-') p++;
        if (p==s.size() || s[p]<'0' || s[p]>'9') return false;
        if (s[p]=='0') p++;
        else while (p<s.size() && s[p]>='0' && s[p]<='9') p++;
        if (p<s.size() && s[p]=='.') {
            size_t first=++p;
            while (p<s.size() && s[p]>='0' && s[p]<='9') p++;
            if (first==p) return false;
        }
        if (p<s.size() && (s[p]=='e' || s[p]=='E')) {
            p++; if (p<s.size() && (s[p]=='+' || s[p]=='-')) p++;
            size_t first=p;
            while (p<s.size() && s[p]>='0' && s[p]<='9') p++;
            if (first==p) return false;
        }
        out=s.substr(start,p-start); return true;
    }
    bool value(JsonValue& v) {
        ws(); if (p==s.size()) return false;
        if (s[p]=='"') { v.type='s'; return str(v.text); }
        if (s[p]=='[') {
            v.type='a'; p++; if (take(']')) return true;
            do {
                std::string token; uint64_t n;
                if (!number(token) || !uint_value(token,n)) return false;
                v.array.push_back(n);
            } while (take(','));
            return take(']');
        }
        if (s.compare(p,4,"true")==0) { p+=4; v.type='b'; v.text="true"; return true; }
        if (s.compare(p,5,"false")==0) { p+=5; v.type='b'; v.text="false"; return true; }
        v.type='n'; return number(v.text);
    }
public:
    std::map<std::string,JsonValue> fields;
    explicit Reader(const std::string& text):s(text) {}
    bool parse() {
        if (!take('{')) return false;
        if (!take('}')) {
            do {
                std::string key; JsonValue v;
                if (!str(key) || !take(':') || !value(v) || !fields.emplace(key,std::move(v)).second) return false;
            } while (take(','));
            if (!take('}')) return false;
        }
        ws(); return p==s.size();
    }
    bool string(const char* key,std::string& out) {
        auto it=fields.find(key); if (it==fields.end() || it->second.type!='s') return false;
        out=it->second.text; return true;
    }
    bool integer(const char* key,uint64_t& out) {
        auto it=fields.find(key); return it!=fields.end() && it->second.type=='n' && uint_value(it->second.text,out);
    }
    bool boolean(const char* key,bool& out) {
        auto it=fields.find(key); if (it==fields.end() || it->second.type!='b') return false;
        out=it->second.text=="true"; return true;
    }
    bool array(const char* key,std::vector<uint64_t>& out) {
        auto it=fields.find(key); if (it==fields.end() || it->second.type!='a') return false;
        out=it->second.array; return true;
    }
    bool real(const char* key,double& out) {
        auto it=fields.find(key); if (it==fields.end() || it->second.type!='n') return false;
        std::istringstream in(it->second.text); in.imbue(std::locale::classic());
        return bool(in>>out) && in.peek()==EOF && std::isfinite(out) && out>=0;
    }
};
} // namespace

bool Checkpoint::load(const std::string& path, std::string& err) {
    std::ifstream f(path);
    if (!f) { err = "cannot open " + path; return false; }
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    if (size < 0 || size > 16 * 1024 * 1024) { err = "checkpoint is unreadable or exceeds 16 MiB"; return false; }
    f.seekg(0);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string s = ss.str();
    Reader r(s); Checkpoint cp;
    uint64_t ver=0, puzzle_id=0;
    std::vector<uint64_t> positions;
    if (!r.parse() || !r.integer("version",ver) || (ver!=1 && ver!=2) ||
        !r.string("tool",cp.tool) || cp.tool!="EXCALIBUR369" ||
        !r.integer("puzzle",puzzle_id) || puzzle_id>160 ||
        !r.string("range_lo",cp.range_lo) || !r.string("range_hi",cp.range_hi) ||
        !r.string("target_hash160",cp.target_hash160) || !r.string("global_order",cp.global_order) ||
        !r.string("local_direction",cp.local_direction) || !r.integer("seed",cp.seed) ||
        !r.boolean("fixed_enabled",cp.fixed_enabled) || !r.string("fixed_mask",cp.fixed_mask) ||
        !r.string("fixed_value",cp.fixed_value) || !r.string("fixed_rotation",cp.fixed_rotation) ||
        !r.array("fixed_positions",positions) || !r.boolean("round_robin",cp.round_robin) ||
        !r.string("slice_keys",cp.slice_keys) || !r.string("segment_size",cp.segment_size) ||
        !r.string("config_hash",cp.config_hash) || !r.integer("watermark",cp.watermark) ||
        !r.array("done_above",cp.done_above) || !r.integer("current_pattern",cp.current_pattern) ||
        !r.string("current_segment",cp.current_segment) || !r.string("local_offset",cp.local_offset) ||
        !r.string("total_candidates",cp.total_candidates) || !r.real("elapsed_seconds",cp.elapsed_seconds)) {
        err="invalid checkpoint JSON, schema or version"; return false;
    }
    for (uint64_t pos:positions) {
        if (pos>31) { err="invalid checkpoint fixed position"; return false; }
        cp.fixed_positions.push_back((int)pos);
    }
    uint8_t bytes[32]; uidx count;
    if (!from_hex(cp.range_lo,bytes,32) || !from_hex(cp.range_hi,bytes,32) ||
        !from_hex(cp.fixed_mask,bytes,32) || !from_hex(cp.fixed_value,bytes,32) ||
        !from_hex(cp.config_hash,bytes,16) || !uidx_from_dec(cp.total_candidates,count) || count>UINT64_MAX) {
        err="invalid checkpoint field value"; return false;
    }
    cp.version=(int)ver; cp.puzzle=(int)puzzle_id;
    if (ver==2) {
        std::string recorded, canonical=cp.payload(); uint8_t hash[32];
        sha256((const uint8_t*)canonical.data(),canonical.size(),hash);
        if (!r.string("integrity_hash",recorded) || recorded!=to_hex(hash,32)) {
            err="checkpoint integrity check failed"; return false;
        }
    }
    *this=std::move(cp);
    return true;
}

std::string config_fingerprint(const SearchConfig& c, const uint8_t mask[32]) {
    std::ostringstream o;
    o << c.range_lo.hex() << "|" << c.range_hi.hex() << "|";
    if (c.has_target) o << to_hex(c.target, 20);
    for (const auto& t : c.extra_targets) o << to_hex(t.data(), 20);
    o << "|"
      << order_name(c.global_order) << "|" << direction_name(c.local_dir) << "|"
      << c.seed << "|" << to_hex(mask, 32) << "|"
      << order_name(c.fixed_rotation) << "|"
      << (c.round_robin ? 1 : 0) << "|" << uidx_to_dec(c.slice_keys) << "|"
      << uidx_to_dec(c.segment_size);
    if (c.fixed.enabled) {
        // Preserve the legacy fingerprint only for searches without a mask.
        // Positions define pattern enumeration order, values define the subset.
        o << "|fixed-v2|" << (c.fixed.values.empty() ? "auto" : "manual") << "|";
        std::vector<int> positions;
        std::string err;
        resolve_positions(c.fixed,c.range_lo,c.range_hi,positions,err);
        for (int pos:positions) o << pos << ",";
        o << "|";
        for (uint8_t v:c.fixed.values) o << (unsigned)v << ",";
    }
    std::string s = o.str();
    uint8_t h[32];
    sha256((const uint8_t*)s.data(), s.size(), h);
    return to_hex(h, 16);
}

} // namespace ex369
