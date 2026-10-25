#include "font_atlas_hd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

namespace mua::font_hd {
namespace {

// Source texels; evens out the 720p stair steps on long diagonals while keeping corners square.
constexpr float kEdgeSmoothing = 0.45f;
// A texel off the line between the two tones by more than this (0-255 RGB distance) is an icon colour.
constexpr float kToneTolerance = 40.0f;

struct Plane {
  uint32_t w = 0;
  uint32_t h = 0;
  std::vector<float> v;

  Plane() = default;
  Plane(uint32_t width, uint32_t height) : w(width), h(height), v(size_t(width) * height, 0.0f) {}
  float& at(uint32_t x, uint32_t y) { return v[size_t(y) * w + x]; }
  float at(uint32_t x, uint32_t y) const { return v[size_t(y) * w + x]; }
};

// Keys' cubic convolution with a = -0.5 (Catmull-Rom), the usual "bicubic".
float Keys(float x) {
  constexpr float a = -0.5f;
  x = std::fabs(x);
  if (x < 1.0f) {
    return ((a + 2.0f) * x - (a + 3.0f)) * x * x + 1.0f;
  }
  if (x < 2.0f) {
    return ((a * x - 5.0f * a) * x + 8.0f * a) * x - 4.0f * a;
  }
  return 0.0f;
}

int Clamp(int v, int lo, int hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Output texel i * n + p samples the source at i + (p + 0.5) / n - 0.5, edges clamped.
Plane Upsample(const Plane& src, uint32_t n) {
  struct Phase {
    int base;
    std::array<float, 4> w;
  };
  std::vector<Phase> phases(n);
  for (uint32_t p = 0; p < n; ++p) {
    const float pos = (float(p) + 0.5f) / float(n) - 0.5f;
    const float base = std::floor(pos);
    const float f = pos - base;
    Phase& phase = phases[p];
    phase.base = int(base) - 1;
    phase.w = {Keys(f + 1.0f), Keys(f), Keys(1.0f - f), Keys(2.0f - f)};
    const float sum = phase.w[0] + phase.w[1] + phase.w[2] + phase.w[3];
    for (float& w : phase.w) {
      w /= sum;
    }
  }
  Plane wide(src.w * n, src.h);
  for (uint32_t y = 0; y < src.h; ++y) {
    for (uint32_t i = 0; i < src.w; ++i) {
      for (uint32_t p = 0; p < n; ++p) {
        const Phase& phase = phases[p];
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
          sum += phase.w[k] * src.at(uint32_t(Clamp(int(i) + phase.base + k, 0, int(src.w) - 1)), y);
        }
        wide.at(i * n + p, y) = sum;
      }
    }
  }
  Plane out(src.w * n, src.h * n);
  for (uint32_t j = 0; j < src.h; ++j) {
    for (uint32_t p = 0; p < n; ++p) {
      const Phase& phase = phases[p];
      int rows[4];
      for (int k = 0; k < 4; ++k) {
        rows[k] = Clamp(int(j) + phase.base + k, 0, int(src.h) - 1);
      }
      for (uint32_t x = 0; x < out.w; ++x) {
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
          sum += phase.w[k] * wide.at(x, uint32_t(rows[k]));
        }
        out.at(x, j * n + p) = sum;
      }
    }
  }
  return out;
}

void Blur(Plane& plane, float sigma) {
  if (sigma <= 0.0f) {
    return;
  }
  const int radius = int(std::ceil(3.0f * sigma));
  std::vector<float> kernel(size_t(2 * radius + 1));
  float sum = 0.0f;
  for (int i = -radius; i <= radius; ++i) {
    kernel[size_t(i + radius)] = std::exp(-0.5f * (float(i) / sigma) * (float(i) / sigma));
    sum += kernel[size_t(i + radius)];
  }
  for (float& k : kernel) {
    k /= sum;
  }
  Plane tmp(plane.w, plane.h);
  for (uint32_t y = 0; y < plane.h; ++y) {
    for (uint32_t x = 0; x < plane.w; ++x) {
      float acc = 0.0f;
      for (int i = -radius; i <= radius; ++i) {
        acc += kernel[size_t(i + radius)] * plane.at(uint32_t(Clamp(int(x) + i, 0, int(plane.w) - 1)), y);
      }
      tmp.at(x, y) = acc;
    }
  }
  for (uint32_t y = 0; y < plane.h; ++y) {
    for (uint32_t x = 0; x < plane.w; ++x) {
      float acc = 0.0f;
      for (int i = -radius; i <= radius; ++i) {
        acc += kernel[size_t(i + radius)] * tmp.at(x, uint32_t(Clamp(int(y) + i, 0, int(plane.h) - 1)));
      }
      plane.at(x, y) = acc;
    }
  }
}

// Coverage of the 0.5 isoline with a one-pixel ramp: (v - 0.5) / |grad v| is the distance to it in pixels.
Plane Sharpen(const Plane& v) {
  Plane out(v.w, v.h);
  auto diff = [](float lo, float hi, uint32_t a, uint32_t b) { return (hi - lo) / float(b - a); };
  for (uint32_t y = 0; y < v.h; ++y) {
    const uint32_t y0 = y > 0 ? y - 1 : y;
    const uint32_t y1 = y + 1 < v.h ? y + 1 : y;
    for (uint32_t x = 0; x < v.w; ++x) {
      const uint32_t x0 = x > 0 ? x - 1 : x;
      const uint32_t x1 = x + 1 < v.w ? x + 1 : x;
      const float gx = x1 > x0 ? diff(v.at(x0, y), v.at(x1, y), x0, x1) : 0.0f;
      const float gy = y1 > y0 ? diff(v.at(x, y0), v.at(x, y1), y0, y1) : 0.0f;
      const float g = std::max(std::sqrt(gx * gx + gy * gy), 1e-4f);
      out.at(x, y) = std::clamp(0.5f + (v.at(x, y) - 0.5f) / g, 0.0f, 1.0f);
    }
  }
  return out;
}

Plane RebuildEdge(const Plane& coverage, uint32_t n) {
  Plane up = Upsample(coverage, n);
  Blur(up, kEdgeSmoothing * float(n));
  return Sharpen(up);
}

uint8_t ToByte(float v) {
  return uint8_t(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}

uint32_t Pack(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
}

Color Unpack(uint32_t c) {
  return {float(c & 0xFF), float((c >> 8) & 0xFF), float((c >> 16) & 0xFF)};
}

bool Inside(const Rect& r, uint32_t x, uint32_t y) {
  return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
}

struct Segment {
  Color o;
  Color d;
  float dd;

  explicit Segment(const TwoTone& tone)
      : o(tone.outline),
        d{tone.fill.r - tone.outline.r, tone.fill.g - tone.outline.g, tone.fill.b - tone.outline.b},
        dd(d.r * d.r + d.g * d.g + d.b * d.b) {}

  // Fill fraction of a colour and its distance to the line.
  float Fraction(const uint8_t* p, float* distance = nullptr) const {
    const float r = p[0] - o.r, g = p[1] - o.g, b = p[2] - o.b;
    const float t = dd > 0.0f ? std::clamp((r * d.r + g * d.g + b * d.b) / dd, 0.0f, 1.0f) : 0.0f;
    if (distance) {
      const float er = r - t * d.r, eg = g - t * d.g, eb = b - t * d.b;
      *distance = std::sqrt(er * er + eg * eg + eb * eb);
    }
    return t;
  }
};

// Face button geometry measured on the games' 24-texel glyphs, in units of a button whose rim radius is kRimOuter.
constexpr float kRimOuter = 10.7f;
constexpr float kRimInner = 9.3f;
constexpr float kLetterHalfHeight = 6.0f;
constexpr float kLetterHalfStroke = 1.65f;
constexpr float kLetterRaise = 0.4f;

struct Vec {
  float x, y;
};

float BoxDistance(Vec p, Vec c, float hx, float hy, float angle = 0.0f) {
  const float cs = std::cos(angle), sn = std::sin(angle);
  const float x = p.x - c.x, y = p.y - c.y;
  const float u = cs * x + sn * y, v = -sn * x + cs * y;
  const float qx = std::fabs(u) - hx, qy = std::fabs(v) - hy;
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f);
}

float SegmentDistance(Vec p, Vec a, Vec b, float half) {
  const Vec m{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
  const float length = std::hypot(b.x - a.x, b.y - a.y);
  return BoxDistance(p, m, length * 0.5f, half, std::atan2(b.y - a.y, b.x - a.x));
}

float RoundBoxDistance(Vec p, Vec c, float hx, float hy, float r) {
  const float qx = std::fabs(p.x - c.x) - hx + r, qy = std::fabs(p.y - c.y) - hy + r;
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}

// Signed distance to the letter, in button units around the button centre (0, 0).
float LetterDistance(FaceButton button, Vec p) {
  const Vec c{0.0f, -kLetterRaise};
  const float h = kLetterHalfHeight, t = kLetterHalfStroke;
  const float frame = BoxDistance(p, c, 6.5f, h);
  switch (button) {
    case FaceButton::kA: {
      float d = std::min(SegmentDistance(p, {c.x, c.y - h}, {c.x - 4.4f, c.y + h}, t),
                         SegmentDistance(p, {c.x, c.y - h}, {c.x + 4.4f, c.y + h}, t));
      d = std::min(d, BoxDistance(p, {c.x, c.y + 1.6f}, 2.6f, 1.1f));
      return std::max(d, frame);
    }
    case FaceButton::kX: {
      const float d = std::min(SegmentDistance(p, {c.x - 4.0f, c.y - h}, {c.x + 4.0f, c.y + h}, t),
                               SegmentDistance(p, {c.x + 4.0f, c.y - h}, {c.x - 4.0f, c.y + h}, t));
      return std::max(d, frame);
    }
    case FaceButton::kY: {
      float d = std::min(SegmentDistance(p, {c.x - 4.2f, c.y - h}, {c.x, c.y + 0.2f}, t),
                         SegmentDistance(p, {c.x + 4.2f, c.y - h}, {c.x, c.y + 0.2f}, t));
      d = std::min(d, BoxDistance(p, {c.x, c.y + h * 0.5f + 0.1f}, t, h * 0.5f - 0.1f));
      return std::max(d, frame);
    }
    case FaceButton::kB: {
      const float stem_x = c.x - 2.6f;
      const float stem = BoxDistance(p, {stem_x, c.y}, t, h);
      const float top = std::fabs(RoundBoxDistance(p, {c.x - 0.6f, c.y - h * 0.5f + 0.05f}, 3.1f, h * 0.5f - 0.05f,
                                                   2.4f)) -
                        t * 0.95f;
      const float bottom = std::fabs(RoundBoxDistance(p, {c.x - 0.3f, c.y + h * 0.5f - 0.05f}, 3.4f,
                                                      h * 0.5f - 0.05f, 2.6f)) -
                           t * 0.95f;
      const float bowls = std::max(std::min(top, bottom), stem_x - p.x);
      return std::min(stem, bowls);
    }
  }
  return 1e9f;
}

float Coverage(float distance, float pixels_per_unit) {
  return std::clamp(0.5f - distance * pixels_per_unit, 0.0f, 1.0f);
}

struct ButtonSample {
  Vec centre;
  float radius;
  Color base;
  Color bright;
};

float Luma(const uint8_t* p) {
  return float(p[0]) + float(p[1]) + float(p[2]);
}

std::optional<ButtonSample> SampleButton(const Atlas& atlas, Rect cell) {
  double sum = 0.0, sx = 0.0, sy = 0.0;
  for (uint32_t y = cell.y0; y < cell.y1; ++y) {
    for (uint32_t x = cell.x0; x < cell.x1; ++x) {
      const float a = atlas.At(x, y)[3] / 255.0f;
      sum += a;
      sx += a * (x - cell.x0 + 0.5);
      sy += a * (y - cell.y0 + 0.5);
    }
  }
  if (sum < 50.0) {
    return std::nullopt;
  }
  ButtonSample s;
  s.centre = {float(sx / sum), float(sy / sum)};
  s.radius = float(std::sqrt(sum / 3.14159265)) - 0.2f;
  const float unit = s.radius / kRimOuter;
  std::vector<const uint8_t*> disc, inner;
  for (uint32_t y = cell.y0; y < cell.y1; ++y) {
    for (uint32_t x = cell.x0; x < cell.x1; ++x) {
      const uint8_t* p = atlas.At(x, y);
      if (p[3] <= 250) {
        continue;
      }
      const float r = std::hypot(x - cell.x0 + 0.5f - s.centre.x, y - cell.y0 + 0.5f - s.centre.y) / unit;
      if (r > 4.0f && r < 8.5f) {
        disc.push_back(p);
      }
      if (r < 7.0f) {
        inner.push_back(p);
      }
    }
  }
  if (disc.size() < 10 || inner.size() < 12) {
    return std::nullopt;
  }
  std::sort(disc.begin(), disc.end(), [](const uint8_t* a, const uint8_t* b) { return Luma(a) < Luma(b); });
  const size_t lo = disc.size() * 3 / 10, hi = std::max(lo + 1, disc.size() * 7 / 10);
  Color base;
  for (size_t i = lo; i < hi; ++i) {
    base.r += disc[i][0];
    base.g += disc[i][1];
    base.b += disc[i][2];
  }
  const float count = float(hi - lo);
  s.base = {base.r / count, base.g / count, base.b / count};
  auto score = [](const uint8_t* p) {
    return Luma(p) + float(std::max({p[0], p[1], p[2]}) - std::min({p[0], p[1], p[2]}));
  };
  std::sort(inner.begin(), inner.end(), [&](const uint8_t* a, const uint8_t* b) { return score(a) > score(b); });
  Color bright;
  for (size_t i = 0; i < 12; ++i) {
    bright.r += inner[i][0];
    bright.g += inner[i][1];
    bright.b += inner[i][2];
  }
  s.bright = {bright.r / 12.0f, bright.g / 12.0f, bright.b / 12.0f};
  return s;
}

Color Mix(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

Color Scale(Color c, float k) {
  return {c.r * k, c.g * k, c.b * k};
}

}  // namespace

Atlas FromBottomUp(uint32_t width, uint32_t height, std::span<const uint8_t> pixels) {
  Atlas atlas{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
  const size_t row = size_t(width) * 4;
  for (uint32_t y = 0; y < height; ++y) {
    std::copy_n(pixels.data() + (height - 1 - y) * row, row, atlas.rgba.data() + y * row);
  }
  return atlas;
}

std::vector<uint8_t> ToBottomUp(const Atlas& atlas) {
  std::vector<uint8_t> pixels(atlas.rgba.size());
  const size_t row = size_t(atlas.width) * 4;
  for (uint32_t y = 0; y < atlas.height; ++y) {
    std::copy_n(atlas.rgba.data() + (atlas.height - 1 - y) * row, row, pixels.data() + y * row);
  }
  return pixels;
}

Rect GlyphRect(const xmlb::Glyph& glyph, uint32_t width, uint32_t height) {
  auto px = [](float f, uint32_t size) { return uint32_t(std::clamp(std::lround(f * float(size)), 0L, long(size))); };
  return {px(glyph.s, width), px(glyph.t, height), px(glyph.s2, width), px(glyph.t2, height)};
}

std::optional<TwoTone> DetectTwoTone(const Atlas& atlas, std::span<const Rect> excluded) {
  std::unordered_map<uint32_t, size_t> inside, edge;
  auto transparent = [&](int x, int y) {
    return x < 0 || y < 0 || x >= int(atlas.width) || y >= int(atlas.height) || atlas.At(uint32_t(x), uint32_t(y))[3] < 255;
  };
  for (uint32_t y = 0; y < atlas.height; ++y) {
    for (uint32_t x = 0; x < atlas.width; ++x) {
      const uint8_t* p = atlas.At(x, y);
      if (p[3] != 255 ||
          std::any_of(excluded.begin(), excluded.end(), [&](const Rect& r) { return Inside(r, x, y); })) {
        continue;
      }
      const bool border = transparent(int(x) - 1, int(y)) || transparent(int(x) + 1, int(y)) ||
                          transparent(int(x), int(y) - 1) || transparent(int(x), int(y) + 1);
      ++(border ? edge : inside)[Pack(p)];
    }
  }
  auto most = [](const std::unordered_map<uint32_t, size_t>& counts) {
    return std::max_element(counts.begin(), counts.end(),
                            [](const auto& a, const auto& b) { return a.second < b.second; });
  };
  if (inside.empty() || edge.empty()) {
    return std::nullopt;
  }
  const uint32_t fill = most(inside)->first;
  const uint32_t outline = most(edge)->first;
  if (fill == outline) {
    return std::nullopt;
  }
  return TwoTone{Unpack(outline), Unpack(fill)};
}

bool FitsTwoTone(const Atlas& atlas, Rect rect, const TwoTone& tone) {
  const Segment segment(tone);
  size_t total = 0, off = 0;
  for (uint32_t y = rect.y0; y < rect.y1; ++y) {
    for (uint32_t x = rect.x0; x < rect.x1; ++x) {
      const uint8_t* p = atlas.At(x, y);
      if (p[3] <= 200) {
        continue;
      }
      float distance = 0.0f;
      segment.Fraction(p, &distance);
      ++total;
      off += distance > kToneTolerance ? 1 : 0;
    }
  }
  return off * 10 <= total;
}

Atlas RebuildText(const Atlas& atlas, uint32_t scale, const TwoTone& tone) {
  const Segment segment(tone);
  Plane outer(atlas.width, atlas.height), inner(atlas.width, atlas.height);
  for (uint32_t y = 0; y < atlas.height; ++y) {
    for (uint32_t x = 0; x < atlas.width; ++x) {
      const uint8_t* p = atlas.At(x, y);
      const float a = p[3] / 255.0f;
      outer.at(x, y) = a;
      inner.at(x, y) = a * segment.Fraction(p);
    }
  }
  const Plane a = RebuildEdge(outer, scale);
  const Plane f = RebuildEdge(inner, scale);
  Atlas out{atlas.width * scale, atlas.height * scale, std::vector<uint8_t>(size_t(a.w) * a.h * 4)};
  for (uint32_t y = 0; y < out.height; ++y) {
    for (uint32_t x = 0; x < out.width; ++x) {
      const float coverage = a.at(x, y);
      const float fill = std::min(f.at(x, y), coverage);
      // Transparent texels keep the outline colour, as the game's atlases do.
      const float t = coverage > 1e-4f ? fill / coverage : 0.0f;
      uint8_t* q = out.At(x, y);
      q[0] = ToByte(segment.o.r + t * segment.d.r);
      q[1] = ToByte(segment.o.g + t * segment.d.g);
      q[2] = ToByte(segment.o.b + t * segment.d.b);
      q[3] = ToByte(coverage * 255.0f);
    }
  }
  return out;
}

void RebuildIcon(const Atlas& atlas, Atlas& out, uint32_t scale, Rect cell) {
  const uint32_t w = cell.x1 - cell.x0, h = cell.y1 - cell.y0;
  if (w == 0 || h == 0) {
    return;
  }
  // Only the cell's own texels: neighbouring icons touch it.
  Plane alpha(w, h), pr(w, h), pg(w, h), pb(w, h);
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const uint8_t* p = atlas.At(cell.x0 + x, cell.y0 + y);
      const float a = p[3] / 255.0f;
      alpha.at(x, y) = a;
      pr.at(x, y) = p[0] * a;
      pg.at(x, y) = p[1] * a;
      pb.at(x, y) = p[2] * a;
    }
  }
  const Plane soft = Upsample(alpha, scale);
  const Plane edge = RebuildEdge(alpha, scale);
  const Plane r = Upsample(pr, scale), g = Upsample(pg, scale), b = Upsample(pb, scale);
  for (uint32_t y = 0; y < h * scale; ++y) {
    for (uint32_t x = 0; x < w * scale; ++x) {
      const float k = 1.0f / std::max(soft.at(x, y), 1e-3f);
      uint8_t* q = out.At(cell.x0 * scale + x, cell.y0 * scale + y);
      q[0] = ToByte(r.at(x, y) * k);
      q[1] = ToByte(g.at(x, y) * k);
      q[2] = ToByte(b.at(x, y) * k);
      q[3] = ToByte(edge.at(x, y) * 255.0f);
    }
  }
}

void DrawFaceButton(const Atlas& atlas, Atlas& out, uint32_t scale, Rect cell, FaceButton button) {
  const auto sample = SampleButton(atlas, cell);
  if (!sample) {
    RebuildIcon(atlas, out, scale, cell);
    return;
  }
  const float unit = sample->radius / kRimOuter;    // texels per button unit
  const float pixels = unit * float(scale);          // output pixels per button unit
  const Color base = sample->base;
  const Color shadow = Scale(base, 0.38f);
  const Color edge_colour = Scale(base, 0.55f);
  const Color rim_top{252.0f, 250.0f, 253.0f};
  const Color rim_bottom{122.0f, 120.0f, 128.0f};
  const float step = 0.5f / pixels;  // half an output pixel, in button units
  const uint32_t w = (cell.x1 - cell.x0) * scale, h = (cell.y1 - cell.y0) * scale;
  for (uint32_t y = 0; y < h; ++y) {
    for (uint32_t x = 0; x < w; ++x) {
      const Vec p{((x + 0.5f) / scale - sample->centre.x) / unit, ((y + 0.5f) / scale - sample->centre.y) / unit};
      const float r = std::hypot(p.x, p.y);
      const float ny = p.y / kRimOuter;
      const float outer = Coverage(r - kRimOuter, pixels);
      const float disc_cover = Coverage(r - kRimInner, pixels);

      const Color rim = Mix(rim_top, rim_bottom, std::clamp(0.5f + 0.55f * ny, 0.0f, 1.0f));
      const float rr = std::clamp(r / kRimInner, 0.0f, 1.0f);
      const float top_band = std::clamp((rr - 0.72f) / 0.28f, 0.0f, 1.0f);
      const float bottom_band = std::clamp((rr - 0.6f) / 0.4f, 0.0f, 1.0f);
      const float shade = 0.95f - 0.22f * ny - 0.12f * (p.x / kRimInner) +
                          0.35f * top_band * top_band * std::clamp(-ny * 1.6f, 0.0f, 1.0f) +
                          0.55f * bottom_band * bottom_band * std::clamp(ny * 1.4f, 0.0f, 1.0f);
      Color colour = Mix(rim, Scale(base, shade), disc_cover);

      const float d = LetterDistance(button, p);
      float extrusion = 1e9f;
      for (int k = 1; k <= 4; ++k) {
        extrusion = std::min(extrusion, LetterDistance(button, {p.x + 0.25f * k, p.y - 0.3f * k}));
      }
      const float face = Coverage(d, pixels);
      const float extruded = std::clamp(Coverage(extrusion, pixels) - face, 0.0f, 1.0f);
      const float rimmed = std::clamp(Coverage(d - 0.3f, pixels) - face, 0.0f, 1.0f);
      // Bevel: the face's edge facing the light (above, to the left) is brighter.
      const float gx = LetterDistance(button, {p.x + step, p.y}) - LetterDistance(button, {p.x - step, p.y});
      const float gy = LetterDistance(button, {p.x, p.y + step}) - LetterDistance(button, {p.x, p.y - step});
      const float facing = (gx * -0.6f + gy * -0.8f) / std::max(std::hypot(gx, gy), 1e-6f);
      const float bevel = std::clamp(1.0f + d / 0.9f, 0.0f, 1.0f);
      const float light = 1.0f + 0.32f * facing * bevel - 0.1f * bevel;
      const float height = std::clamp((p.y + kLetterRaise + kLetterHalfHeight) / (2.0f * kLetterHalfHeight), 0.0f, 1.0f);
      const Color letter = Scale(sample->bright, (1.06f - 0.16f * height) * light);

      colour = Mix(colour, shadow, extruded);
      colour = Mix(colour, edge_colour, rimmed);
      colour = Mix(colour, letter, face);
      uint8_t* q = out.At(cell.x0 * scale + x, cell.y0 * scale + y);
      q[0] = ToByte(colour.r);
      q[1] = ToByte(colour.g);
      q[2] = ToByte(colour.b);
      q[3] = ToByte(outer * 255.0f);
    }
  }
}

std::optional<Atlas> BuildHdAtlas(const Atlas& atlas, const xmlb::FontTable& table, uint32_t scale,
                                  std::span<const IconRule> icons, BuildReport* report) {
  if (scale < 1 || atlas.width == 0 || atlas.height == 0 || atlas.rgba.size() != size_t(atlas.width) * atlas.height * 4) {
    return std::nullopt;
  }
  struct Cell {
    Rect rect;
    const IconRule* rule;
  };
  std::vector<Cell> cells;
  std::vector<Rect> listed;
  for (const xmlb::Glyph& glyph : table.glyphs) {
    const Rect rect = GlyphRect(glyph, atlas.width, atlas.height);
    if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0) {
      continue;
    }
    const auto rule = std::find_if(icons.begin(), icons.end(), [&](const IconRule& r) { return r.code == glyph.code; });
    cells.push_back({rect, rule != icons.end() ? &*rule : nullptr});
    if (rule != icons.end()) {
      listed.push_back(rect);
    }
  }
  const auto tone = DetectTwoTone(atlas, listed);
  if (!tone) {
    return std::nullopt;
  }
  BuildReport local;
  local.tone = *tone;
  Atlas out = RebuildText(atlas, scale, *tone);
  for (const Cell& cell : cells) {
    if (cell.rule && cell.rule->face) {
      DrawFaceButton(atlas, out, scale, cell.rect, *cell.rule->face);
      ++local.face_buttons;
    } else if (cell.rule) {
      RebuildIcon(atlas, out, scale, cell.rect);
      ++local.icons;
    } else if (!FitsTwoTone(atlas, cell.rect, *tone)) {
      RebuildIcon(atlas, out, scale, cell.rect);
      ++local.unlisted_icons;
    } else {
      ++local.text_glyphs;
    }
  }
  if (report) {
    *report = local;
  }
  return out;
}

}  // namespace mua::font_hd
