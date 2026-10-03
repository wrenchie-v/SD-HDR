// Inverse tone map from an SDR desktop into the monitor's HDR range.
// Keep this in sync with EvaluateTonemap() in src/ToneMapParams.cpp.
#pragma pack_matrix(row_major)

cbuffer Params : register(b0)
{
    float sdrWhiteNits;
    float peakNits;
    float minNits;
    float knee;
    float highlightGain;
    float shadowLift;
    float saturation;
    float gamutExpansion;
    float uiProtect;
    float contrast;
    float enabled;
    float inputMode;
    float outputMode;
    float testPattern;
    float midGray;
    float decodeMode;
    float4x4 m709toDisp;
    float4x4 mDispTo709;
};

Texture2D srcTex : register(t0);
SamplerState samp : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

static const float3 kLuma = float3(0.2126, 0.7152, 0.0722);

// FXC drops unused cbuffer members and repacks the rest. Touch every uniform.
void KeepUniforms(inout float3 nits)
{
    float probe = minNits + decodeMode + midGray + testPattern + enabled + inputMode + outputMode
        + sdrWhiteNits + peakNits + knee + highlightGain + shadowLift + saturation
        + gamutExpansion + uiProtect + contrast
        + m709toDisp[0][0] + m709toDisp[3][3] + mDispTo709[0][0] + mDispTo709[3][3];
    [branch]
    if (probe == 1234567.0)
    {
        nits.r += 0.001;
    }
}

float3 ToLinear(float3 c)
{
    if (decodeMode < 0.5)
    {
        float3 lo = c / 12.92;
        float3 hi = pow(saturate((c + 0.055) / 1.055), 2.4);
        return lerp(hi, lo, step(c, float3(0.04045, 0.04045, 0.04045)));
    }
    return pow(max(c, 0.0), 2.2);
}

float Luma(float3 c)
{
    return dot(max(c, 0.0), kLuma);
}

// RTX HDR / Auto HDR shape, not a hard knee:
// paper white stays near the Windows SDR white level, speculars roll up
// toward a fraction of peak, and the slope is continuous so 8-bit gradients
// do not break into bands. highlightGain is how much of the headroom above
// paper white is used. knee is where that roll starts.
float3 Tonemap(float3 nits)
{
    float Y = Luma(nits);
    if (Y < 1e-4)
    {
        return nits;
    }

    float W = max(sdrWhiteNits, 1.0);
    float P = max(peakNits, W);
    float x0 = Y / W;
    float native = smoothstep(1.08, 1.55, x0);

    float x = x0;
    if (shadowLift > 0.001)
    {
        float lifted = pow(max(saturate(x), 1e-6), rcp(1.0 + shadowLift * 0.35));
        float wgt = (1.0 - smoothstep(0.15, 0.65, x)) * saturate(shadowLift);
        x = lerp(x, lifted, wgt);
    }

    float pivot = clamp(midGray, 0.05, 0.5);
    x = pow(max(x, 1e-6) / pivot, max(contrast, 0.5)) * pivot;

    float peakRatio = min(P / W, 3.2);
    float gain = saturate(highlightGain);
    float satCeiling = 1.0 + gain * (peakRatio - 1.0);
    float start = clamp(knee, 0.35, 0.85);
    float t = smoothstep(start, 1.0, saturate(x));
    float shaped = x * (1.0 + (satCeiling - 1.0) * t);

    float maxC = max(nits.r, max(nits.g, nits.b));
    float minC = min(nits.r, min(nits.g, nits.b));
    float sat = (maxC - minC) / max(maxC, 1e-4);
    float nearWhite = smoothstep(start, 1.0, saturate(x0));
    float achromatic = 1.0 - smoothstep(0.04, 0.22, sat);
    float protect = saturate(uiProtect) * nearWhite * achromatic;
    float uiCeiling = 1.0 + 0.08 * gain;
    float uiShaped = x * (1.0 + (uiCeiling - 1.0) * t);
    shaped = lerp(shaped, uiShaped, protect);
    shaped = lerp(shaped, x0, native);

    float outY = min(max(shaped, 0.0) * W, P);
    return nits * (outY / Y);
}

float3 Grade(float3 nits, float expandWeight)
{
    float y = Luma(nits);
    float3 c = lerp(y.xxx, nits, saturation);
    float chroma = saturate(gamutExpansion) * saturate(expandWeight);
    if (chroma > 0.001)
    {
        float3 disp = mul(m709toDisp, float4(c, 0.0)).xyz;
        float yd = Luma(max(disp, 0.0));
        disp = yd.xxx + (disp - yd.xxx) * (1.0 + chroma);
        c = mul(mDispTo709, float4(disp, 0.0)).xyz;
    }
    return c;
}

float3 ApplyCurve(float3 nits)
{
    float yIn = max(Luma(nits), 1e-4);
    float3 mapped = Tonemap(nits);
    float weight = saturate(Luma(mapped) / yIn - 1.0);
    return Grade(mapped, weight);
}

float3 Rec709To2020(float3 c)
{
    return mul(float3x3(
        0.6274039, 0.3292830, 0.0433131,
        0.0690973, 0.9195404, 0.0113623,
        0.0163914, 0.0880133, 0.8955953), c);
}

float ToPQ(float nits)
{
    float Y = saturate(nits / 10000.0);
    const float m1 = 0.1593017578125;
    const float m2 = 78.84375;
    const float c1 = 0.8359375;
    const float c2 = 18.8515625;
    const float c3 = 18.6875;
    float yp = pow(max(Y, 0.0), m1);
    return pow((c1 + c2 * yp) / max(1.0 + c3 * yp, 1e-6), m2);
}

float Dither(float2 pix)
{
    return frac(sin(dot(pix, float2(12.9898, 78.233))) * 43758.5453) - 0.5;
}

float3 Encode(float3 nits, float2 pix)
{
    if (outputMode < 0.5)
    {
        float3 sc = nits / 80.0;
        sc += Dither(pix) * (1.0 / 2048.0);
        return sc;
    }
    float3 rec = max(Rec709To2020(nits), 0.0);
    float3 pq = float3(ToPQ(rec.r), ToPQ(rec.g), ToPQ(rec.b));
    pq += Dither(pix) * (1.0 / 1024.0);
    return saturate(pq);
}

float3 SampleNits(float2 uv)
{
    float4 s = srcTex.SampleLevel(samp, uv, 0);
    if (inputMode < 0.5)
    {
        return s.rgb * 80.0;
    }
    return ToLinear(s.rgb) * max(sdrWhiteNits, 1.0);
}

float PatchValue(int index, float peak)
{
    float values[14] = { 0, 1, 5, 20, 80, 100, 200, 400, 600, 1000, 2000, 4000, peak, peak };
    return values[clamp(index, 0, 13)];
}

float3 Pattern(float2 uv)
{
    float W = max(sdrWhiteNits, 80.0);
    float P = max(peakNits, W);

    if (uv.y < 0.10)
    {
        return float3(W, W, W);
    }

    if (uv.y < 0.40)
    {
        float t = saturate(uv.x);
        float n = t * P;
        float3 c = n.xxx;
        float ticks[6] = { 80, 200, 400, 600, 1000, 2000 };
        [unroll]
        for (int i = 0; i < 6; ++i)
        {
            if (ticks[i] < P && abs(n - ticks[i]) < P * 0.004)
            {
                c *= 0.35;
            }
        }
        return c;
    }

    if (uv.y < 0.58)
    {
        int count = 12;
        int index = clamp(int(uv.x * count), 0, count - 1);
        float n = PatchValue(index, P);
        float3 c = n.xxx;
        float edge = abs(frac(uv.x * count) - 0.5);
        if (edge > 0.46)
        {
            c *= 0.2;
        }
        if (n > P + 0.5 && frac(uv.x * 40.0) > 0.5)
        {
            c = float3(P, 0, 0);
        }
        return c;
    }

    if (uv.y < 0.76)
    {
        int index = clamp(int(uv.x * 8.0), 0, 7);
        float3 bars[8] = {
            float3(1, 0, 0), float3(0, 1, 0), float3(0, 0, 1), float3(0, 1, 1),
            float3(1, 0, 1), float3(1, 1, 0), float3(1, 1, 1), float3(1, 0.85, 0.7)
        };
        float level = (uv.y < 0.67) ? 100.0 : min(P, 600.0);
        return bars[index] * level;
    }

    float half = (uv.x < 0.5) ? saturate(uv.x * 2.0) : saturate((uv.x - 0.5) * 2.0);
    float3 code = (uv.y < 0.88) ? half.xxx : float3(half, half * 0.12, half * 0.08);
    float3 nits = ToLinear(code) * W;
    if (uv.x >= 0.5)
    {
        nits = ApplyCurve(nits);
    }
    if (abs(uv.x - 0.5) < 0.003)
    {
        nits = float3(0, 0, 0);
    }
    return nits;
}

float4 main(VSOut input) : SV_Target
{
    float3 nits = 0;
    KeepUniforms(nits);

    if (testPattern > 1.5)
    {
        return float4(Luma(SampleNits(input.uv)), 0, 0, 1);
    }

    if (testPattern > 0.5)
    {
        nits = Pattern(input.uv);
    }
    else
    {
        nits = SampleNits(input.uv);
        if (enabled >= 0.5)
        {
            nits = ApplyCurve(nits);
        }
    }

    return float4(Encode(nits, input.pos.xy), 1);
}
