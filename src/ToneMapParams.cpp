#include "ToneMapParams.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace {

float Saturate(float v) { return Clamp(v, 0.f, 1.f); }

float SmoothStep(float e0, float e1, float x) {
    const float t = Saturate((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

float Luma(Vec3 c) {
    return std::max(c.x, 0.f) * 0.2126f + std::max(c.y, 0.f) * 0.7152f + std::max(c.z, 0.f) * 0.0722f;
}

bool Invert3(const float m[9], float out[9]) {
    const float a = m[0], b = m[1], c = m[2];
    const float d = m[3], e = m[4], f = m[5];
    const float g = m[6], h = m[7], i = m[8];
    const float c00 = e * i - f * h;
    const float c01 = f * g - d * i;
    const float c02 = d * h - e * g;
    const float c10 = c * h - b * i;
    const float c11 = a * i - c * g;
    const float c12 = b * g - a * h;
    const float c20 = b * f - c * e;
    const float c21 = c * d - a * f;
    const float c22 = a * e - b * d;
    const float det = a * c00 + b * c01 + c * c02;
    if (std::fabs(det) < 1e-8f) {
        return false;
    }
    const float inv = 1.f / det;
    out[0] = c00 * inv; out[1] = c10 * inv; out[2] = c20 * inv;
    out[3] = c01 * inv; out[4] = c11 * inv; out[5] = c21 * inv;
    out[6] = c02 * inv; out[7] = c12 * inv; out[8] = c22 * inv;
    return true;
}

void Mul3(const float a[9], const float b[9], float o[9]) {
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            o[r * 3 + c] = a[r * 3 + 0] * b[0 * 3 + c] + a[r * 3 + 1] * b[1 * 3 + c] + a[r * 3 + 2] * b[2 * 3 + c];
        }
    }
}

bool RgbToXyz(float rx, float ry, float gx, float gy, float bx, float by, float wx, float wy, float m[9]) {
    if (ry < 1e-4f || gy < 1e-4f || by < 1e-4f || wy < 1e-4f) {
        return false;
    }
    const float xr = rx / ry, yr = 1.f, zr = (1.f - rx - ry) / ry;
    const float xg = gx / gy, yg = 1.f, zg = (1.f - gx - gy) / gy;
    const float xb = bx / by, yb = 1.f, zb = (1.f - bx - by) / by;
    const float xw = wx / wy, yw = 1.f, zw = (1.f - wx - wy) / wy;
    const float prim[9] = {xr, xg, xb, yr, yg, yb, zr, zg, zb};
    float inv[9];
    if (!Invert3(prim, inv)) {
        return false;
    }
    const float s0 = inv[0] * xw + inv[1] * yw + inv[2] * zw;
    const float s1 = inv[3] * xw + inv[4] * yw + inv[5] * zw;
    const float s2 = inv[6] * xw + inv[7] * yw + inv[8] * zw;
    m[0] = s0 * xr; m[1] = s1 * xg; m[2] = s2 * xb;
    m[3] = s0 * yr; m[4] = s1 * yg; m[5] = s2 * yb;
    m[6] = s0 * zr; m[7] = s1 * zg; m[8] = s2 * zb;
    return true;
}

void Store4(float m[16], const float a[9]) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = a[0]; m[1] = a[1]; m[2] = a[2];
    m[4] = a[3]; m[5] = a[4]; m[6] = a[5];
    m[8] = a[6]; m[9] = a[7]; m[10] = a[8];
    m[15] = 1.f;
}

Vec3 Mul4(const float m[16], Vec3 v) {
    return {
        m[0] * v.x + m[1] * v.y + m[2] * v.z,
        m[4] * v.x + m[5] * v.y + m[6] * v.z,
        m[8] * v.x + m[9] * v.y + m[10] * v.z,
    };
}

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::vector<std::pair<std::string, Json>> o;

    const Json* Find(const std::string& key) const {
        if (type != Type::Object) {
            return nullptr;
        }
        for (const auto& item : o) {
            if (item.first == key) {
                return &item.second;
            }
        }
        return nullptr;
    }
    bool Bool(const char* key, bool fallback) const {
        const Json* v = Find(key);
        return v && v->type == Type::Bool ? v->b : fallback;
    }
    double Num(const char* key, double fallback) const {
        const Json* v = Find(key);
        return v && v->type == Type::Number ? v->n : fallback;
    }
    std::string Str(const char* key, const std::string& fallback) const {
        const Json* v = Find(key);
        return v && v->type == Type::String ? v->s : fallback;
    }
};

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Json Parse() {
        Skip();
        Json v = ParseValue();
        return v;
    }

private:
    std::string_view text_;
    size_t i_ = 0;

    void Skip() {
        while (i_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[i_]))) {
            ++i_;
        }
    }
    bool Eat(char c) {
        Skip();
        if (i_ < text_.size() && text_[i_] == c) {
            ++i_;
            return true;
        }
        return false;
    }
    Json ParseValue() {
        Skip();
        if (i_ >= text_.size()) {
            return {};
        }
        const char c = text_[i_];
        if (c == '{') return ParseObject();
        if (c == '[') return ParseArray();
        if (c == '"') return ParseString();
        if (c == 't' || c == 'f') return ParseBool();
        if (c == 'n') { i_ += 4; return {}; }
        return ParseNumber();
    }
    Json ParseObject() {
        Json v;
        v.type = Json::Type::Object;
        Eat('{');
        Skip();
        if (Eat('}')) return v;
        for (;;) {
            Json key = ParseString();
            Eat(':');
            Json child = ParseValue();
            v.o.emplace_back(key.s, std::move(child));
            Skip();
            if (Eat('}')) break;
            Eat(',');
        }
        return v;
    }
    Json ParseArray() {
        Json v;
        v.type = Json::Type::Array;
        Eat('[');
        Skip();
        if (Eat(']')) return v;
        for (;;) {
            v.a.push_back(ParseValue());
            Skip();
            if (Eat(']')) break;
            Eat(',');
        }
        return v;
    }
    Json ParseString() {
        Json v;
        v.type = Json::Type::String;
        if (!Eat('"')) return v;
        while (i_ < text_.size() && text_[i_] != '"') {
            if (text_[i_] == '\\' && i_ + 1 < text_.size()) {
                const char e = text_[++i_];
                if (e == 'n') v.s.push_back('\n');
                else if (e == 't') v.s.push_back('\t');
                else if (e == 'r') v.s.push_back('\r');
                else if (e == 'u' && i_ + 4 < text_.size()) {
                    int cp = 0;
                    for (int k = 0; k < 4; ++k) {
                        const char h = text_[++i_];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp += h - '0';
                        else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                    }
                    if (cp < 0x80) v.s.push_back(static_cast<char>(cp));
                    else if (cp < 0x800) {
                        v.s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        v.s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        v.s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        v.s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        v.s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                } else v.s.push_back(e);
                ++i_;
            } else {
                v.s.push_back(text_[i_++]);
            }
        }
        Eat('"');
        return v;
    }
    Json ParseBool() {
        Json v;
        v.type = Json::Type::Bool;
        if (text_.substr(i_, 4) == "true") { v.b = true; i_ += 4; }
        else { v.b = false; i_ += 5; }
        return v;
    }
    Json ParseNumber() {
        Json v;
        v.type = Json::Type::Number;
        size_t start = i_;
        if (text_[i_] == '-') ++i_;
        while (i_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[i_])) || text_[i_] == '.' || text_[i_] == 'e' || text_[i_] == 'E' || text_[i_] == '+' || text_[i_] == '-')) {
            ++i_;
        }
        try {
            v.n = std::stod(std::string(text_.substr(start, i_ - start)));
        } catch (...) {
            v.n = 0;
        }
        return v;
    }
};

std::string Escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '\\': o += "\\\\"; break;
        case '"': o += "\\\""; break;
        case '\n': o += "\\n"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                sprintf_s(buf, "\\u%04x", c);
                o += buf;
            } else {
                o.push_back(static_cast<char>(c));
            }
        }
    }
    return o;
}

void ReadParams(const Json& j, ToneMapParams& p) {
    p.peakNits = static_cast<float>(j.Num("peakNits", p.peakNits));
    p.sdrWhiteOverride = static_cast<float>(j.Num("sdrWhiteOverride", p.sdrWhiteOverride));
    p.contrast = static_cast<float>(j.Num("contrast", p.contrast));
    p.knee = static_cast<float>(j.Num("knee", p.knee));
    p.highlightGain = static_cast<float>(j.Num("highlightGain", p.highlightGain));
    p.shadowLift = static_cast<float>(j.Num("shadowLift", p.shadowLift));
    p.saturation = static_cast<float>(j.Num("saturation", p.saturation));
    p.gamutExpansion = static_cast<float>(j.Num("gamutExpansion", p.gamutExpansion));
    p.uiProtect = static_cast<float>(j.Num("uiProtect", p.uiProtect));
    p.midGray = static_cast<float>(j.Num("midGray", p.midGray));
    const std::string decode = j.Str("decodeMode", p.decodeMode == 0 ? "srgb" : "gamma");
    p.decodeMode = decode == "gamma" ? 1 : 0;
    const std::string output = j.Str("outputMode", p.outputMode == 0 ? "scrgb" : "pq");
    p.outputMode = output == "pq" ? 1 : 0;
}

void WriteParams(std::ostringstream& o, const ToneMapParams& p, int indent) {
    const std::string pad(indent, ' ');
    o << pad << "\"peakNits\": " << p.peakNits << ",\n";
    o << pad << "\"sdrWhiteOverride\": " << p.sdrWhiteOverride << ",\n";
    o << pad << "\"contrast\": " << p.contrast << ",\n";
    o << pad << "\"knee\": " << p.knee << ",\n";
    o << pad << "\"highlightGain\": " << p.highlightGain << ",\n";
    o << pad << "\"shadowLift\": " << p.shadowLift << ",\n";
    o << pad << "\"saturation\": " << p.saturation << ",\n";
    o << pad << "\"gamutExpansion\": " << p.gamutExpansion << ",\n";
    o << pad << "\"uiProtect\": " << p.uiProtect << ",\n";
    o << pad << "\"midGray\": " << p.midGray << ",\n";
    o << pad << "\"decodeMode\": \"" << (p.decodeMode == 1 ? "gamma" : "srgb") << "\",\n";
    o << pad << "\"outputMode\": \"" << (p.outputMode == 1 ? "pq" : "scrgb") << "\"";
}

const char* PolicyName(AppPolicy policy) {
    switch (policy) {
    case AppPolicy::ForceOn: return "forceOn";
    case AppPolicy::ForceOff: return "forceOff";
    default: return "auto";
    }
}

AppPolicy PolicyFromName(const std::string& name) {
    if (name == "forceOn") return AppPolicy::ForceOn;
    if (name == "forceOff") return AppPolicy::ForceOff;
    return AppPolicy::Auto;
}

}  // namespace

AppSettings::AppSettings() {
    globalParams = MakePreset(PresetId::Natural);
}

ToneMapParams MakePreset(PresetId preset) {
    ToneMapParams p;
    p.peakNits = 0.f;
    p.sdrWhiteOverride = 0.f;
    p.midGray = 0.18f;
    p.decodeMode = 0;
    p.outputMode = 0;
    switch (preset) {
    case PresetId::Bright:
        p.contrast = 1.03f;
        p.knee = 0.52f;
        p.highlightGain = 0.68f;
        p.shadowLift = 0.05f;
        p.saturation = 1.05f;
        p.gamutExpansion = 0.16f;
        p.uiProtect = 0.84f;
        break;
    case PresetId::Vivid:
        p.contrast = 1.05f;
        p.knee = 0.42f;
        p.highlightGain = 0.85f;
        p.shadowLift = 0.04f;
        p.saturation = 1.12f;
        p.gamutExpansion = 0.28f;
        p.uiProtect = 0.75f;
        break;
    case PresetId::Custom:
    case PresetId::Natural:
    default:
        // Paper white stays put. Saturated highlights use about half of the
        // headroom above it, and white UI is held near the Windows slider.
        p.contrast = 1.f;
        p.knee = 0.62f;
        p.highlightGain = 0.50f;
        p.shadowLift = 0.f;
        p.saturation = 1.f;
        p.gamutExpansion = 0.10f;
        p.uiProtect = 0.92f;
        break;
    }
    return p;
}

PresetId PresetFromName(const std::wstring& name) {
    if (EqualsIgnoreCase(name, L"Bright")) return PresetId::Bright;
    if (EqualsIgnoreCase(name, L"Vivid")) return PresetId::Vivid;
    if (EqualsIgnoreCase(name, L"Custom")) return PresetId::Custom;
    return PresetId::Natural;
}

const wchar_t* PresetName(PresetId preset) {
    switch (preset) {
    case PresetId::Bright: return L"Bright";
    case PresetId::Vivid: return L"Vivid";
    case PresetId::Custom: return L"Custom";
    default: return L"Natural";
    }
}

float ResolvePeak(const ToneMapParams& params, float monitorPeak) {
    const float mon = monitorPeak > 10.f ? monitorPeak : 1000.f;
    if (params.peakNits <= 1.f) {
        return mon;
    }
    return std::min(params.peakNits, mon);
}

float ResolveWhite(const ToneMapParams& params, float windowsWhite) {
    if (params.sdrWhiteOverride >= 80.f) {
        return params.sdrWhiteOverride;
    }
    return windowsWhite >= 1.f ? windowsWhite : 80.f;
}

void IdentityMatrix(float m[16]) {
    std::memset(m, 0, sizeof(float) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.f;
}

bool BuildGamutMatrices(const float primaries[8], float toDisp[16], float to709[16]) {
    float rec709[9]{};
    if (!RgbToXyz(0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f, rec709)) {
        IdentityMatrix(toDisp);
        IdentityMatrix(to709);
        return false;
    }
    float disp[9]{};
    const bool given = primaries
        && primaries[1] > 0.01f && primaries[3] > 0.01f && primaries[5] > 0.01f && primaries[7] > 0.01f
        && RgbToXyz(primaries[0], primaries[1], primaries[2], primaries[3], primaries[4], primaries[5], primaries[6], primaries[7], disp);
    if (!given) {
        // DCI-P3 D65, a typical wide desktop gamut when the display does not report primaries.
        if (!RgbToXyz(0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f, 0.3127f, 0.3290f, disp)) {
            IdentityMatrix(toDisp);
            IdentityMatrix(to709);
            return false;
        }
    }
    float invDisp[9]{};
    float inv709[9]{};
    if (!Invert3(disp, invDisp) || !Invert3(rec709, inv709)) {
        IdentityMatrix(toDisp);
        IdentityMatrix(to709);
        return false;
    }
    float mToDisp[9]{};
    float mTo709[9]{};
    Mul3(invDisp, rec709, mToDisp);
    Mul3(inv709, disp, mTo709);
    Store4(toDisp, mToDisp);
    Store4(to709, mTo709);
    return true;
}

Vec3 EvaluateTonemap(Vec3 nits, const ToneMapParams& params, float sdrWhite, float peak, const float toDisp[16], const float to709[16]) {
    const float Y = Luma(nits);
    if (Y < 1e-4f) {
        return nits;
    }
    const float W = std::max(sdrWhite, 1.f);
    const float P = std::max(peak, W);
    const float x0 = Y / W;
    const float native = SmoothStep(1.08f, 1.55f, x0);
    float x = x0;
    if (params.shadowLift > 0.001f) {
        const float lifted = std::pow(std::max(Saturate(x), 1e-6f), 1.f / (1.f + params.shadowLift * 0.35f));
        const float wgt = (1.f - SmoothStep(0.15f, 0.65f, x)) * Saturate(params.shadowLift);
        x = Lerp(x, lifted, wgt);
    }
    const float pivot = Clamp(params.midGray, 0.05f, 0.5f);
    x = std::pow(std::max(x, 1e-6f) / pivot, std::max(params.contrast, 0.5f)) * pivot;
    const float peakRatio = std::min(P / W, 3.2f);
    const float gain = Saturate(params.highlightGain);
    const float satCeiling = 1.f + gain * (peakRatio - 1.f);
    const float start = Clamp(params.knee, 0.35f, 0.85f);
    const float t = SmoothStep(start, 1.f, Saturate(x));
    float shaped = x * (1.f + (satCeiling - 1.f) * t);
    const float maxC = std::max(nits.x, std::max(nits.y, nits.z));
    const float minC = std::min(nits.x, std::min(nits.y, nits.z));
    const float sat = (maxC - minC) / std::max(maxC, 1e-4f);
    const float nearWhite = SmoothStep(start, 1.f, Saturate(x0));
    const float achromatic = 1.f - SmoothStep(0.04f, 0.22f, sat);
    const float protect = Saturate(params.uiProtect) * nearWhite * achromatic;
    const float uiCeiling = 1.f + 0.08f * gain;
    const float uiShaped = x * (1.f + (uiCeiling - 1.f) * t);
    shaped = Lerp(shaped, uiShaped, protect);
    shaped = Lerp(shaped, x0, native);
    const float outY = std::min(std::max(shaped, 0.f) * W, P);
    Vec3 graded{nits.x * (outY / Y), nits.y * (outY / Y), nits.z * (outY / Y)};

    const float yIn = std::max(Y, 1e-4f);
    const float yOut = Luma(graded);
    const float expandWeight = Saturate(yOut / yIn - 1.f);
    const float y2 = yOut;
    graded = {
        Lerp(y2, graded.x, params.saturation),
        Lerp(y2, graded.y, params.saturation),
        Lerp(y2, graded.z, params.saturation),
    };
    const float chroma = Saturate(params.gamutExpansion) * expandWeight;
    if (chroma > 0.001f && toDisp && to709) {
        Vec3 disp = Mul4(toDisp, graded);
        const float yd = Luma(Vec3{std::max(disp.x, 0.f), std::max(disp.y, 0.f), std::max(disp.z, 0.f)});
        const float boost = 1.f + chroma;
        disp = {yd + (disp.x - yd) * boost, yd + (disp.y - yd) * boost, yd + (disp.z - yd) * boost};
        graded = Mul4(to709, disp);
    }
    return graded;
}

MonitorSettings* FindMonitorSettings(AppSettings& settings, const std::wstring& device) {
    for (auto& item : settings.monitors) {
        if (EqualsIgnoreCase(item.first, device)) {
            return &item.second;
        }
    }
    return nullptr;
}

const MonitorSettings* FindMonitorSettings(const AppSettings& settings, const std::wstring& device) {
    for (const auto& item : settings.monitors) {
        if (EqualsIgnoreCase(item.first, device)) {
            return &item.second;
        }
    }
    return nullptr;
}

bool LoadSettings(AppSettings& settings, const std::wstring& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    Json root = Parser(text).Parse();
    if (root.type != Json::Type::Object) {
        return false;
    }
    const int loadedVersion = static_cast<int>(root.Num("version", 1));
    settings.version = loadedVersion;
    settings.enabled = root.Bool("enabled", settings.enabled);
    settings.autostart = root.Bool("autostart", settings.autostart);
    settings.startMinimized = root.Bool("startMinimized", settings.startMinimized);
    settings.hotkeyEnabled = root.Bool("hotkeyEnabled", settings.hotkeyEnabled);
    settings.showHud = root.Bool("showHud", settings.showHud);
    settings.captureCursor = root.Bool("captureCursor", settings.captureCursor);
    settings.calibrationPattern = root.Bool("calibrationPattern", false);
    settings.gpuBudgetMs = static_cast<float>(root.Num("gpuBudgetMs", settings.gpuBudgetMs));
    settings.sawTrayHint = root.Bool("sawTrayHint", false);
    const std::string backend = root.Str("captureBackend", "auto");
    settings.captureBackend = backend == "wgc" ? 1 : backend == "duplication" ? 2 : 0;
    settings.activeProfile = WideFromUtf8(root.Str("activeProfile", "Natural"));
    if (const Json* global = root.Find("global")) {
        ReadParams(*global, settings.globalParams);
    }
    if (const Json* window = root.Find("window")) {
        settings.windowX = static_cast<int>(window->Num("x", settings.windowX));
        settings.windowY = static_cast<int>(window->Num("y", settings.windowY));
        settings.windowW = static_cast<int>(window->Num("w", settings.windowW));
        settings.windowH = static_cast<int>(window->Num("h", settings.windowH));
    }
    settings.rules.clear();
    if (const Json* rules = root.Find("rules"); rules && rules->type == Json::Type::Array) {
        for (const Json& rule : rules->a) {
            AppRule item;
            item.process = WideFromUtf8(rule.Str("process", ""));
            item.policy = PolicyFromName(rule.Str("mode", "auto"));
            if (!item.process.empty()) {
                settings.rules.push_back(std::move(item));
            }
        }
    }
    settings.monitors.clear();
    if (const Json* monitors = root.Find("monitors"); monitors && monitors->type == Json::Type::Array) {
        for (const Json& monitor : monitors->a) {
            MonitorSettings item;
            item.inherit = monitor.Bool("inherit", true);
            item.disabled = monitor.Bool("disabled", false);
            item.params = settings.globalParams;
            ReadParams(monitor, item.params);
            const std::wstring device = WideFromUtf8(monitor.Str("device", ""));
            if (!device.empty()) {
                settings.monitors.emplace_back(device, item);
            }
        }
    }
    if (loadedVersion < 2) {
        const PresetId preset = PresetFromName(settings.activeProfile);
        if (preset != PresetId::Custom) {
            settings.globalParams = MakePreset(preset);
            for (auto& monitor : settings.monitors) {
                if (monitor.second.inherit) {
                    monitor.second.params = settings.globalParams;
                }
            }
        }
        settings.version = 2;
        SaveSettings(settings, path);
    }
    return true;
}

bool SaveSettings(const AppSettings& settings, const std::wstring& path) {
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": 2,\n";
    o << "  \"enabled\": " << (settings.enabled ? "true" : "false") << ",\n";
    o << "  \"autostart\": " << (settings.autostart ? "true" : "false") << ",\n";
    o << "  \"startMinimized\": " << (settings.startMinimized ? "true" : "false") << ",\n";
    o << "  \"hotkeyEnabled\": " << (settings.hotkeyEnabled ? "true" : "false") << ",\n";
    o << "  \"showHud\": " << (settings.showHud ? "true" : "false") << ",\n";
    o << "  \"captureCursor\": " << (settings.captureCursor ? "true" : "false") << ",\n";
    o << "  \"calibrationPattern\": " << (settings.calibrationPattern ? "true" : "false") << ",\n";
    o << "  \"sawTrayHint\": " << (settings.sawTrayHint ? "true" : "false") << ",\n";
    o << "  \"gpuBudgetMs\": " << settings.gpuBudgetMs << ",\n";
    const char* backend = settings.captureBackend == 1 ? "wgc" : settings.captureBackend == 2 ? "duplication" : "auto";
    o << "  \"captureBackend\": \"" << backend << "\",\n";
    o << "  \"activeProfile\": \"" << Escape(Utf8FromWide(settings.activeProfile)) << "\",\n";
    o << "  \"window\": { \"x\": " << settings.windowX << ", \"y\": " << settings.windowY
      << ", \"w\": " << settings.windowW << ", \"h\": " << settings.windowH << " },\n";
    o << "  \"global\": {\n";
    WriteParams(o, settings.globalParams, 4);
    o << "\n  },\n";
    o << "  \"rules\": [\n";
    for (size_t i = 0; i < settings.rules.size(); ++i) {
        o << "    { \"process\": \"" << Escape(Utf8FromWide(settings.rules[i].process))
          << "\", \"mode\": \"" << PolicyName(settings.rules[i].policy) << "\" }";
        if (i + 1 < settings.rules.size()) o << ",";
        o << "\n";
    }
    o << "  ],\n";
    o << "  \"monitors\": [\n";
    for (size_t i = 0; i < settings.monitors.size(); ++i) {
        const auto& item = settings.monitors[i];
        o << "    {\n";
        o << "      \"device\": \"" << Escape(Utf8FromWide(item.first)) << "\",\n";
        o << "      \"inherit\": " << (item.second.inherit ? "true" : "false") << ",\n";
        o << "      \"disabled\": " << (item.second.disabled ? "true" : "false") << ",\n";
        WriteParams(o, item.second.params, 6);
        o << "\n    }";
        if (i + 1 < settings.monitors.size()) o << ",";
        o << "\n";
    }
    o << "  ]\n";
    o << "}\n";

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    const std::string text = o.str();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return true;
}

bool ValidateColorMath(std::wstring& detail) {
    float m[9]{};
    if (!RgbToXyz(0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f, m)) {
        detail = L"Rec.709 matrix failed to build";
        return false;
    }
    const float ref[9] = {
        0.4124564f, 0.3575761f, 0.1804375f,
        0.2126729f, 0.7151522f, 0.0721750f,
        0.0193339f, 0.1191920f, 0.9503041f,
    };
    float worst = 0.f;
    for (int i = 0; i < 9; ++i) {
        worst = std::max(worst, std::fabs(m[i] - ref[i]));
    }
    if (worst > 2e-3f) {
        detail = L"Rec.709 matrix does not match the reference";
        return false;
    }
    const float prim[8] = {0.64f, 0.33f, 0.30f, 0.60f, 0.15f, 0.06f, 0.3127f, 0.3290f};
    float toDisp[16]{};
    float to709[16]{};
    if (!BuildGamutMatrices(prim, toDisp, to709)) {
        detail = L"Gamut matrices failed";
        return false;
    }
    const Vec3 back = Mul4(to709, Mul4(toDisp, Vec3{1.f, 0.f, 0.f}));
    if (std::fabs(back.x - 1.f) > 1e-3f || std::fabs(back.y) > 1e-3f || std::fabs(back.z) > 1e-3f) {
        detail = L"Gamut round-trip did not preserve Rec.709 red";
        return false;
    }
    detail = L"color math ok";
    return true;
}

bool ValidateSettingsRoundtrip(std::wstring& detail) {
    AppSettings settings;
    settings.enabled = false;
    settings.globalParams.highlightGain = 0.42f;
    settings.activeProfile = L"Custom";
    settings.rules.push_back(AppRule{L"game.exe", AppPolicy::ForceOff});
    settings.monitors.push_back({L"\\\\.\\DISPLAY1", MonitorSettings{false, false, settings.globalParams}});
    const std::wstring path = AppDataDir() + L"\\selftest-settings.json";
    if (!SaveSettings(settings, path)) {
        detail = L"Could not write settings";
        return false;
    }
    AppSettings loaded;
    if (!LoadSettings(loaded, path)) {
        detail = L"Could not read settings";
        return false;
    }
    DeleteFileW(path.c_str());
    if (loaded.enabled || std::fabs(loaded.globalParams.highlightGain - 0.42f) > 1e-3f || loaded.rules.size() != 1
        || loaded.monitors.size() != 1 || loaded.monitors[0].second.inherit) {
        detail = L"Settings round-trip changed a value";
        return false;
    }
    detail = L"settings round-trip ok";
    return true;
}

void SetRunAtStartup(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0,
            KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    if (enabled) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring cmd = L"\"" + std::wstring(path) + L"\" --minimized";
        RegSetValueExW(key, L"SD-HDR", 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, L"SD-HDR");
    }
    RegCloseKey(key);
}
