#include "chart_internal.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <locale>
#include <sstream>
#include <cmath>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_set>

namespace huxerui::charts_detail {
namespace {

double Add(double a, double b) {
  const double value = a + b;
  Require(std::isfinite(value), "Chart stacking overflowed the finite numeric range");
  return value;
}

bool Stacked(const Config& c) {
  return (c.kind == Kind::Area && c.area_mode != AreaMode::Unstacked) ||
         (c.kind == Kind::Bar && c.bar_mode != BarMode::Grouped);
}
bool Percent(const Config& c) {
  return (c.kind == Kind::Area && c.area_mode == AreaMode::PercentStacked) ||
         (c.kind == Kind::Bar && c.bar_mode == BarMode::PercentStacked);
}
void Include(double value, double& minimum, double& maximum) {
  minimum = std::min(minimum, value);
  maximum = std::max(maximum, value);
}

float Coordinate(double value, ChartNumericDomain domain, float origin, float length) {
  // Offscreen anchors remain finite. Connected geometry is clipped in data space before projection.
  const double coordinate = origin + Fraction(value, domain) * length;
  constexpr double limit = std::numeric_limits<float>::max() / 4.0;
  return static_cast<float>(std::clamp(coordinate, -limit, limit));
}

std::uint64_t Cell(int x, int y) {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(y);
}

float Distance(Point a, Point b) {
  return static_cast<float>(std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y));
}
float SegmentDistance(Point p, Point a, Point b) {
  const double dx = static_cast<double>(b.x) - a.x;
  const double dy = static_cast<double>(b.y) - a.y;
  const double length = dx * dx + dy * dy;
  const double t = length == 0.0 ? 0.0 : std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / length, 0.0, 1.0);
  return static_cast<float>(std::hypot(p.x - std::lerp(static_cast<double>(a.x), static_cast<double>(b.x), t),
                                       p.y - std::lerp(static_cast<double>(a.y), static_cast<double>(b.y), t)));
}

struct MarkerOutline {
  std::array<Point, 12> points;
  std::size_t count = 0;
};

// All polygon markers use the requested filled area. Painting, bounds and hit distances share these vertices
// so changing shape does not silently change bubble magnitude or leave a circular hit region behind.
MarkerOutline Outline(Point center, float area, ChartMarkerShape shape) {
  MarkerOutline outline;
  const auto add = [&](float x, float y) { outline.points[outline.count++] = {center.x + x, center.y + y}; };
  switch (shape) {
    case ChartMarkerShape::Circle: break;
    case ChartMarkerShape::Square: {
      const float half = std::sqrt(area) * 0.5F;
      add(-half, -half); add(half, -half); add(half, half); add(-half, half);
      break;
    }
    case ChartMarkerShape::Diamond: {
      const float half = std::sqrt(area * 0.5F);
      add(0, -half); add(half, 0); add(0, half); add(-half, 0);
      break;
    }
    case ChartMarkerShape::Triangle: {
      const float half = std::sqrt(area / std::sqrt(3.0F));
      const float height = half * std::sqrt(3.0F);
      add(0, -height * 2.0F / 3.0F); add(half, height / 3.0F); add(-half, height / 3.0F);
      break;
    }
    case ChartMarkerShape::Cross: {
      const float arm = std::sqrt(area / 5.0F) * 0.5F;
      add(-arm, -arm * 3); add(arm, -arm * 3); add(arm, -arm); add(arm * 3, -arm);
      add(arm * 3, arm); add(arm, arm); add(arm, arm * 3); add(-arm, arm * 3);
      add(-arm, arm); add(-arm * 3, arm); add(-arm * 3, -arm); add(-arm, -arm);
      break;
    }
  }
  return outline;
}

float MarkerDistance(const Mark& mark, Point point) {
  if (mark.shape == ChartMarkerShape::Circle || mark.area == 0.0F) {
    return std::max(0.0F, Distance(point, mark.point) - std::sqrt(mark.area / std::numbers::pi_v<float>));
  }
  // Interior points have zero distance; outside points use the nearest edge, including the cross's concave edges.
  // Hit() adds tolerance to this filled boundary, independently of the painted stroke style.
  if (MarkerPath(mark.point, mark.area, mark.shape).Contains(point, PathFillRule::NonZero)) return 0.0F;
  const auto outline = Outline(mark.point, mark.area, mark.shape);
  float distance = std::numeric_limits<float>::infinity();
  for (std::size_t i = 0; i < outline.count; ++i) {
    distance = std::min(distance, SegmentDistance(point, outline.points[i], outline.points[(i + 1) % outline.count]));
  }
  return distance;
}

std::vector<std::size_t> SampleIndices(const std::vector<ModelSample>& samples, ChartNumericDomain domain,
                                     float width, bool simplify) {
  if (samples.empty()) return {};
  auto first = std::lower_bound(samples.begin(), samples.end(), domain.min,
                               [](const ModelSample& p, double x) { return p.x < x; });
  auto last = std::upper_bound(samples.begin(), samples.end(), domain.max,
                              [](double x, const ModelSample& p) { return x < p.x; });
  // Keep a neighbor on each side: a segment may cross the viewport even when neither endpoint is inside it.
  if (first != samples.begin()) --first;
  if (last != samples.end()) ++last;
  const auto begin = static_cast<std::size_t>(first - samples.begin());
  const auto end = static_cast<std::size_t>(last - samples.begin());
  std::vector<std::size_t> out;
  if (!simplify || end - begin <= static_cast<std::size_t>(std::max(1.0F, width)) * 4) {
    for (auto i = begin; i < end; ++i) out.push_back(i);
    return out;
  }
  std::size_t i = begin;
  // Keep bucket endpoints and extrema in source order. Gaps end buckets and remain explicit contour breaks.
  while (i < end) {
    if (samples[i].gap) { out.push_back(i++); continue; }
    const auto bucket = std::floor(std::clamp(Fraction(samples[i].x, domain), -1.0, 2.0) * width);
    const auto start = i;
    auto low = i;
    auto high = i;
    while (++i < end && !samples[i].gap &&
           std::floor(std::clamp(Fraction(samples[i].x, domain), -1.0, 2.0) * width) == bucket) {
      if (samples[i].upper < samples[low].upper) low = i;
      if (samples[i].upper > samples[high].upper) high = i;
    }
    std::array<std::size_t, 4> selected{start, low, high, i - 1};
    std::sort(selected.begin(), selected.end());
    for (auto index : selected) if (out.empty() || out.back() != index) out.push_back(index);
  }
  return out;
}

NumericPoint Lerp(NumericPoint a, NumericPoint b, double t) {
  return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t)};
}

Point Project(NumericPoint point, const Geometry& geometry) {
  return {Coordinate(point.x, geometry.x_domain, geometry.plot.x, geometry.plot.width),
          Coordinate(point.y, geometry.y_domain, geometry.plot.y + geometry.plot.height, -geometry.plot.height)};
}

struct ClipEdge {
  bool horizontal;
  double value;
  bool minimum;

  bool Inside(NumericPoint point) const {
    const double coordinate = horizontal ? point.y : point.x;
    return minimum ? coordinate >= value : coordinate <= value;
  }

  NumericPoint Intersection(NumericPoint a, NumericPoint b) const {
    const double start = horizontal ? a.y : a.x;
    const double end = horizontal ? b.y : b.x;
    auto point = Lerp(a, b, Fraction(value, {start, end}));
    // Snap the clipped coordinate exactly to its edge so rounding cannot leave it outside the next clip pass.
    if (horizontal) point.y = value;
    else point.x = value;
    return point;
  }
};

std::array<ClipEdge, 4> ClipEdges(const Geometry& geometry) {
  return {{{false, geometry.x_domain.min, true}, {false, geometry.x_domain.max, false},
           {true, geometry.y_domain.min, true}, {true, geometry.y_domain.max, false}}};
}

// Build and clip connected geometry from numeric source coordinates, not the bounded float marker anchors.
// Each generated segment retains original mark owners, so interpolation and clipping create no synthetic data.
void MakeContour(Contour& contour, const Geometry& geometry, ChartInterpolation interpolation) {
  if (contour.marks.empty()) return;
  const auto& marks = geometry.marks;
  std::vector<NumericPoint> points;
  for (auto i : contour.marks) points.push_back(marks[i].source);
  std::vector<NumericPoint> curve{points.front()};
  std::vector<std::size_t> owners{contour.marks.front()};
  const auto append = [&](NumericPoint point, std::size_t owner) {
    curve.push_back(point);
    owners.push_back(owner);
  };
  std::vector<double> slopes(points.size(), 0.0);
  ChartNumericDomain y_range{points.front().y, points.front().y};
  for (auto point : points) Include(point.y, y_range.min, y_range.max);
  const ChartNumericDomain x_range{points.front().x, points.back().x};
  const bool monotone = interpolation == ChartInterpolation::Monotone && points.size() > 1 &&
                        y_range.min < y_range.max && x_range.min < x_range.max;
  if (monotone) {
    // Normalize before computing tangents to reduce overflow risk with widely separated numeric values.
    // Harmonic tangents join same-sign secants; a sign change leaves a flat tangent at the local extremum.
    std::vector<double> secants(points.size() - 1);
    for (std::size_t i = 0; i < secants.size(); ++i) {
      const double dx = Fraction(points[i + 1].x, x_range) - Fraction(points[i].x, x_range);
      const double dy = Fraction(points[i + 1].y, y_range) - Fraction(points[i].y, y_range);
      secants[i] = dx > 0.0 ? dy / dx : 0.0;
    }
    slopes.front() = secants.front();
    slopes.back() = secants.back();
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
      if ((secants[i - 1] > 0.0 && secants[i] > 0.0) || (secants[i - 1] < 0.0 && secants[i] < 0.0)) {
        slopes[i] = 2.0 / (1.0 / secants[i - 1] + 1.0 / secants[i]);
      }
    }
  }
  const auto edges = ClipEdges(geometry);
  for (std::size_t i = 1; i < points.size(); ++i) {
    const auto a = points[i - 1];
    const auto b = points[i];
    if (interpolation == ChartInterpolation::StepBefore) append({a.x, b.y}, contour.marks[i]);
    if (interpolation == ChartInterpolation::StepAfter) append({b.x, a.y}, contour.marks[i - 1]);
    if (monotone) {
      const double dx = Fraction(b.x, x_range) - Fraction(a.x, x_range);
      const auto ordinate = [&](NumericPoint point, double slope, double direction) {
        const double delta = std::isfinite(slope) ? slope * dx / 3.0 : 0.0;
        const double normalized = std::clamp(Fraction(point.y, y_range) + direction * delta, 0.0, 1.0);
        return std::clamp(std::lerp(y_range.min, y_range.max, normalized), std::min(a.y, b.y), std::max(a.y, b.y));
      };
      const NumericPoint c1{std::lerp(a.x, b.x, 1.0 / 3.0), ordinate(a, slopes[i - 1], 1.0)};
      const NumericPoint c2{std::lerp(a.x, b.x, 2.0 / 3.0), ordinate(b, slopes[i], -1.0)};
      // A Bezier lies in its control hull, so an entirely offscreen hull needs no further subdivision.
      // Otherwise flatten toward a subpixel error, with a depth cap to bound work on extreme curves.
      const auto flatten = [&](auto&& self, NumericPoint p0, NumericPoint p1, NumericPoint p2,
                               NumericPoint p3, double end, int depth) -> void {
        const bool outside = std::any_of(edges.begin(), edges.end(), [&](const ClipEdge& edge) {
          return !edge.Inside(p0) && !edge.Inside(p1) && !edge.Inside(p2) && !edge.Inside(p3);
        });
        const auto start = Project(p0, geometry);
        const auto finish = Project(p3, geometry);
        if (outside || depth == 16 ||
            std::max(SegmentDistance(Project(p1, geometry), start, finish),
                     SegmentDistance(Project(p2, geometry), start, finish)) <= 0.25F) {
          append(p3, end <= 0.5 ? contour.marks[i - 1] : contour.marks[i]);
          return;
        }
        const auto q0 = Lerp(p0, p1, 0.5);
        const auto q1 = Lerp(p1, p2, 0.5);
        const auto q2 = Lerp(p2, p3, 0.5);
        const auto r0 = Lerp(q0, q1, 0.5);
        const auto r1 = Lerp(q1, q2, 0.5);
        const auto mid = Lerp(r0, r1, 0.5);
        self(self, p0, q0, r0, mid, end - std::ldexp(1.0, -depth - 1), depth + 1);
        self(self, mid, r1, q2, p3, end, depth + 1);
      };
      flatten(flatten, a, c1, c2, b, 1.0, 0);
    } else {
      append(b, contour.marks[i]);
    }
  }
  std::optional<Point> last;
  for (std::size_t i = 1; i < curve.size(); ++i) {
    auto a = curve[i - 1];
    auto b = curve[i];
    bool visible = true;
    for (const auto& edge : edges) {
      const bool inside_a = edge.Inside(a);
      const bool inside_b = edge.Inside(b);
      if (!inside_a && !inside_b) { visible = false; break; }
      if (inside_a != inside_b) {
        const auto intersection = edge.Intersection(a, b);
        if (!inside_a) a = intersection;
        else b = intersection;
      }
    }
    if (!visible) { last.reset(); continue; }
    const auto start = Project(a, geometry);
    const auto end = Project(b, geometry);
    // Separate visible runs must not be joined along a clipping boundary after an offscreen excursion.
    if (!last || *last != start) contour.line.MoveTo(start);
    contour.line.LineTo(end);
    contour.segments.push_back({start, end, owners[i - 1], owners[i]});
    last = end;
  }
  if (geometry.kind != Kind::Area) return;
  // Return along the baseline in reverse order to close the fill while preserving the chosen step direction.
  for (std::size_t n = contour.marks.size(); n > 0; --n) {
    const auto p = marks[contour.marks[n - 1]].source_baseline;
    curve.push_back(p);
    if (n > 1) {
      const auto previous = marks[contour.marks[n - 2]].source_baseline;
      if (interpolation == ChartInterpolation::StepBefore) curve.push_back({previous.x, p.y});
      if (interpolation == ChartInterpolation::StepAfter) curve.push_back({p.x, previous.y});
    }
  }
  // Clip the closed area independently so viewport edges do not become artificial stroked segments.
  for (const auto& edge : edges) {
    std::vector<NumericPoint> clipped;
    if (curve.empty()) break;
    auto previous = curve.back();
    for (auto point : curve) {
      if (edge.Inside(point) != edge.Inside(previous)) clipped.push_back(edge.Intersection(previous, point));
      if (edge.Inside(point)) clipped.push_back(point);
      previous = point;
    }
    curve = std::move(clipped);
  }
  if (!curve.empty()) {
    contour.fill.MoveTo(Project(curve.front(), geometry));
    for (std::size_t i = 1; i < curve.size(); ++i) contour.fill.LineTo(Project(curve[i], geometry));
    contour.fill.Close();
  }
}

// Index every cell touched by a mark's visible bounds, not just its center, so large bubbles remain hittable.
// Connected line segments are checked separately by Hit(); this index is only the mark broad phase.
void IndexGeometry(Geometry& geometry) {
  geometry.cells.clear();
  const auto plot = geometry.plot;
  for (std::size_t i = 0; i < geometry.marks.size(); ++i) {
    const auto& mark = geometry.marks[i];
    Rect bounds = mark.bounds.Intersection(plot);
    if (bounds.IsEmpty() && !plot.Contains(mark.point)) continue;
    if (bounds.IsEmpty()) bounds = {mark.point.x, mark.point.y, 1.0F, 1.0F};
    const int left = static_cast<int>(std::floor((bounds.x - plot.x) / 32.0F));
    const int top = static_cast<int>(std::floor((bounds.y - plot.y) / 32.0F));
    const int right = static_cast<int>(std::floor((bounds.x + bounds.width - plot.x) / 32.0F));
    const int bottom = static_cast<int>(std::floor((bounds.y + bounds.height - plot.y) / 32.0F));
    for (int y = top; y <= bottom; ++y) for (int x = left; x <= right; ++x) geometry.cells[Cell(x, y)].push_back(i);
  }
}

} // namespace

Path MarkerPath(Point center, float area, ChartMarkerShape shape) {
  Path path;
  if (area <= 0.0F) return path;
  if (shape == ChartMarkerShape::Circle) {
    const float radius = std::sqrt(area / std::numbers::pi_v<float>);
    path.MoveTo({center.x - radius, center.y})
        .ArcTo({radius, radius}, 0.0F, ArcSize::Small, ArcDirection::Clockwise, {center.x + radius, center.y})
        .ArcTo({radius, radius}, 0.0F, ArcSize::Small, ArcDirection::Clockwise, {center.x - radius, center.y}).Close();
  } else {
    const auto outline = Outline(center, area, shape);
    path.MoveTo(outline.points[0]);
    for (std::size_t i = 1; i < outline.count; ++i) path.LineTo(outline.points[i]);
    path.Close();
  }
  return path;
}

Rect MarkerBounds(Point center, float area, ChartMarkerShape shape) {
  if (shape == ChartMarkerShape::Circle) {
    const float radius = std::sqrt(area / std::numbers::pi_v<float>);
    return {center.x - radius, center.y - radius, radius * 2.0F, radius * 2.0F};
  }
  const auto outline = Outline(center, area, shape);
  Point minimum = center;
  Point maximum = center;
  for (std::size_t i = 0; i < outline.count; ++i) {
    minimum.x = std::min(minimum.x, outline.points[i].x);
    minimum.y = std::min(minimum.y, outline.points[i].y);
    maximum.x = std::max(maximum.x, outline.points[i].x);
    maximum.y = std::max(maximum.y, outline.points[i].y);
  }
  return {minimum.x, minimum.y, maximum.x - minimum.x, maximum.y - minimum.y};
}

std::string Number(double value) {
  std::ostringstream stream;
  stream.imbue(std::locale::classic());
  stream << std::setprecision(6) << value;
  return stream.str();
}

std::uint64_t StableHash(std::string_view value) {
  // Fixed FNV-1a keeps key-derived colors and semantic IDs independent of the standard library's hash choice.
  std::uint64_t hash = 14695981039346656037ULL;
  for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; }
  return hash;
}

double Fraction(double value, ChartNumericDomain domain) {
  const double span = domain.max - domain.min;
  if (std::isfinite(span)) {
    const double delta = value - domain.min;
    if (std::isfinite(delta)) return delta / span;
  }
  // Halving before subtraction preserves the ratio when finite endpoints have an overflowing difference.
  // Out-of-domain values are deliberately not clamped here; geometric clipping owns their visibility.
  return (value * 0.5 - domain.min * 0.5) / (domain.max * 0.5 - domain.min * 0.5);
}

ChartNumericDomain AutoDomain(double min, double max, bool zero) {
  if (!std::isfinite(min) || !std::isfinite(max)) return {0.0, 1.0};
  if (zero) { min = std::min(min, 0.0); max = std::max(max, 0.0); }
  if (min == max) {
    const double pad = std::max(1.0, std::abs(min) * 0.05);
    const double lower = min - pad;
    const double upper = max + pad;
    // At a finite numeric extreme only the inward side can expand without producing infinity.
    if (std::isfinite(lower)) min = lower;
    if (std::isfinite(upper)) max = upper;
  }
  ValidateDomain({min, max});
  return {min, max};
}

void ValidateConfig(const Config& c) {
  Require(Access::Category(c.x) == (c.data->kind == DataKind::Category), "Chart horizontal/category axis has the wrong scale");
  Require(!Access::Category(c.y), "Chart value axis must be linear");
  Require(std::isfinite(c.baseline), "Area baseline must be finite");
  Require(!(c.kind == Kind::Area && Stacked(c) && c.interpolation == ChartInterpolation::Monotone),
          "Stacked areas support linear and step interpolation, not monotone interpolation");
  Require(!(c.kind == Kind::Area && Stacked(c) && c.baseline != 0.0), "Stacked areas use a zero baseline");
  Require(c.accessibility.max_data_nodes >= 1 && c.accessibility.max_data_nodes <= 512,
          "Chart accessibility node limit must be between 1 and 512");
  Require(std::isfinite(c.animation.update.duration) && c.animation.update.duration >= 0.0,
          "Chart animation duration must be finite and nonnegative");
  const auto& size = c.size_scale;
  Require(std::isfinite(size.min_area) && std::isfinite(size.max_area) && size.min_area >= 0.0F &&
          size.max_area >= size.min_area, "Scatter marker area range must be finite, nonnegative and increasing");
  if (size.domain) {
    ValidateDomain(*size.domain);
    Require(size.domain->min >= 0.0, "Scatter magnitude domain cannot be negative");
  }
  for (const auto& [key, style] : c.series_styles) {
    Require(std::any_of(c.data->entries.begin(), c.data->entries.end(), [&](const Entry& entry) { return entry.key == key; }),
            "SeriesStyle references an unknown series key");
    Require(style.index() == c.style.index(), "SeriesStyle does not match its chart component");
  }
}

// Only data-space changes invalidate domains and stacks. Sampling, orientation and styling belong to later stages.
bool SameModel(const Config& a, const Config& b) {
  return a.data == b.data && a.kind == b.kind && a.area_mode == b.area_mode && a.bar_mode == b.bar_mode &&
         a.baseline == b.baseline && Access::Domain(a.x) == Access::Domain(b.x) &&
         Access::Domain(a.y) == Access::Domain(b.y) && Access::Zero(a.x) == Access::Zero(b.x) &&
         Access::Zero(a.y) == Access::Zero(b.y);
}

Model BuildModel(const Config& c) {
  ValidateConfig(c);
  Model result;
  result.series.resize(c.data->entries.size());
  const double inf = std::numeric_limits<double>::infinity();
  double xmin = inf, xmax = -inf, ymin = inf, ymax = -inf, mmin = inf, mmax = -inf;
  std::vector<std::size_t> visible;
  for (std::size_t s = 0; s < c.data->entries.size(); ++s) if (c.data->entries[s].visible) visible.push_back(s);
  std::vector<double> grid;
  if (Stacked(c)) {
    // Stacks need a shared abscissa. Categories supply that grid; numeric areas require an exact match
    // rather than silently resampling another series or interpolating missing business values.
    if (c.data->kind == DataKind::Category) {
      for (std::size_t i = 0; i < c.data->categories->size(); ++i) grid.push_back(static_cast<double>(i));
    } else if (!visible.empty()) {
      for (const auto& sample : c.data->entries[visible.front()].index->samples) grid.push_back(sample.x);
      for (auto s : visible) {
        const auto& samples = c.data->entries[s].index->samples;
        Require(samples.size() == grid.size(), "Stacked numeric areas require the same x grid");
        for (std::size_t i = 0; i < grid.size(); ++i) Require(samples[i].x == grid[i], "Stacked numeric areas require the same x grid");
      }
    }
  }
  std::vector<double> positive(grid.size(), 0.0), negative(grid.size(), 0.0), total(grid.size(), 0.0);
  std::vector<bool> gap(grid.size(), false);
  // Determine shared area gaps and percentage denominators before accumulating individual series.
  // Missing bars omit only their segment, while a missing area member breaks the whole stack at that x.
  if (Stacked(c)) {
    for (auto s : visible) {
      const auto& samples = c.data->entries[s].index->samples;
      std::size_t cursor = 0;
      for (std::size_t i = 0; i < grid.size(); ++i) {
        while (cursor < samples.size() && samples[cursor].x < grid[i]) ++cursor;
        if (cursor == samples.size() || samples[cursor].x != grid[i] || !samples[cursor].y) {
          if (c.kind == Kind::Area) gap[i] = true;
        } else if (Percent(c)) {
          Require(*samples[cursor].y >= 0.0, "Percentage stacks require nonnegative values");
          total[i] = Add(total[i], *samples[cursor].y);
        }
      }
    }
  }
  for (auto s : visible) {
    const auto& samples = c.data->entries[s].index->samples;
    auto& out = result.series[s];
    out.reserve(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
      const auto& sample = samples[i];
      ModelSample p{.x = sample.x, .lower = c.kind == Kind::Area ? c.baseline : 0.0,
                    .upper = sample.y.value_or(0.0), .index = i, .gap = !sample.y};
      if (Stacked(c)) {
        const auto g = c.data->kind == DataKind::Category ? static_cast<std::size_t>(sample.x) : i;
        p.gap = p.gap || gap[g] || (Percent(c) && total[g] == 0.0);
        if (!p.gap) {
          const double value = Percent(c) ? *sample.y / total[g] : *sample.y;
          if (Percent(c)) p.normalized = value;
          // Positive and negative contributions grow away from zero instead of canceling one another.
          auto& sum = value >= 0.0 ? positive[g] : negative[g];
          p.lower = sum;
          sum = Add(sum, value);
          p.upper = sum;
        }
      }
      if (!p.gap) {
        Include(p.x, xmin, xmax);
        Include(p.upper, ymin, ymax);
        if (c.kind == Kind::Area || c.kind == Kind::Bar) Include(p.lower, ymin, ymax);
        if (sample.magnitude) Include(*sample.magnitude, mmin, mmax);
        ++result.count;
      }
      out.push_back(p);
    }
  }
  if (Stacked(c) && c.kind == Kind::Bar) {
    // Walk backward to mark the outermost nonzero segment on each side; internal joins stay square.
    std::vector<bool> seen_positive(grid.size()), seen_negative(grid.size());
    for (auto s = visible.rbegin(); s != visible.rend(); ++s) {
      for (auto& p : result.series[*s]) {
        if (p.gap || p.lower == p.upper) { p.exposed = false; continue; }
        auto& seen = p.upper > p.lower ? seen_positive : seen_negative;
        const auto i = static_cast<std::size_t>(p.x);
        p.exposed = !seen[i];
        seen[i] = true;
      }
    }
  }
  result.x = Access::Domain(c.x).value_or(AutoDomain(xmin, xmax, Access::Zero(c.x)));
  if (c.data->kind == DataKind::Category) result.x = {-0.5, std::max(0.5, static_cast<double>(c.data->categories->size()) - 0.5)};
  result.y = Access::Domain(c.y).value_or(Percent(c) ? ChartNumericDomain{0.0, 1.0} : AutoDomain(ymin, ymax, Access::Zero(c.y)));
  result.magnitude = std::isfinite(mmin) ? ChartNumericDomain{mmin, mmax} : ChartNumericDomain{0.0, 1.0};
  return result;
}

std::optional<Mark> ProjectMark(const Config& c, const Model& model, Rect plot, std::size_t series,
                                const ModelSample& p, const ChartMarkerStyle& marker,
                                std::size_t slot, std::size_t visible_count) {
  if (plot.IsEmpty() || p.gap) return {};
  Mark mark{.series = series, .sample = p.index, .exposed = p.exposed, .normalized = p.normalized};
  const float x = Coordinate(p.x, model.x, plot.x, plot.width);
  const float y = Coordinate(p.upper, model.y, plot.y + plot.height, -plot.height);
  const float base = Coordinate(p.lower, model.y, plot.y + plot.height, -plot.height);
  mark.point = {x, y};
  mark.baseline = {x, base};
  // Keep numeric endpoints alongside screen anchors for slope-preserving line and area clipping.
  mark.source = {p.x, p.upper};
  mark.source_baseline = {p.x, p.lower};
  mark.area = marker.area.value_or(36.0F);
  mark.shape = marker.shape.value_or(ChartMarkerShape::Circle);
  const auto& raw = c.data->entries[series].index->samples[p.index];
  if (c.kind == Kind::Scatter && raw.magnitude) {
    // Magnitude maps linearly to filled area, not radius; a constant domain uses the area-range midpoint.
    const auto domain = c.size_scale.domain.value_or(model.magnitude);
    const double t = domain.min == domain.max ? 0.5 : std::clamp(Fraction(*raw.magnitude, domain), 0.0, 1.0);
    mark.area = static_cast<float>(std::lerp(static_cast<double>(c.size_scale.min_area),
                                           static_cast<double>(c.size_scale.max_area), t));
  }
  mark.bounds = MarkerBounds(mark.point, mark.area, mark.shape);
  if (c.kind == Kind::Bar) {
    // Group slots belong to visible series, so an absent datum leaves its slot empty without shifting peers.
    const bool horizontal = c.orientation == ChartOrientation::Horizontal;
    const float band = (horizontal ? plot.height : plot.width) /
                       static_cast<float>(std::max<std::size_t>(1, c.data->categories->size()));
    const float count = c.bar_mode == BarMode::Grouped ? static_cast<float>(visible_count) : 1.0F;
    const float thickness = band * 0.8F / count;
    const float shift = c.bar_mode == BarMode::Grouped ? thickness * (static_cast<float>(slot) + 0.5F - count / 2.0F) : 0.0F;
    if (horizontal) {
      const float cy = Coordinate(p.x, model.x, plot.y, plot.height) + shift;
      const float end = Coordinate(p.upper, model.y, plot.x, plot.width);
      const float start = Coordinate(p.lower, model.y, plot.x, plot.width);
      mark.point = {end, cy};
      mark.baseline = {start, cy};
      mark.bounds = {std::min(start, end), cy - thickness * 0.46F, std::abs(end - start), thickness * 0.92F};
    } else {
      mark.point.x += shift;
      mark.baseline.x += shift;
      mark.bounds = {x + shift - thickness * 0.46F, std::min(y, base), thickness * 0.92F, std::abs(y - base)};
    }
  }
  // Connected charts retain offscreen anchors for clipping; isolated marks must have visible bounds.
  if ((c.kind == Kind::Scatter || c.kind == Kind::Bar) && !mark.bounds.Intersects(plot)) return {};
  return mark;
}

Geometry BuildGeometry(const Config& c, const Model& model, Rect plot, const std::vector<ChartMarkerStyle>& markers,
                       std::span<const ChartDatumIdentity> pinned) {
  Geometry result{.kind = c.kind, .plot = plot, .x_domain = model.x, .y_domain = model.y};
  if (plot.IsEmpty()) return result;
  std::size_t visible_count = 0;
  for (const auto& e : c.data->entries) if (e.visible) ++visible_count;
  std::set<std::size_t> common;
  if (c.kind == Kind::Area && Stacked(c) && c.data->kind == DataKind::XY && c.sampling == ChartSampling::Auto) {
    // Every stack member must retain the same x positions or adjacent area boundaries can separate.
    for (const auto& samples : model.series) {
      auto indices = SampleIndices(samples, model.x, plot.width, true);
      common.insert(indices.begin(), indices.end());
    }
  }
  std::size_t slot = 0;
  for (std::size_t s = 0; s < model.series.size(); ++s) {
    if (!c.data->entries[s].visible) continue;
    const auto& samples = model.series[s];
    std::vector<std::size_t> indices;
    if (c.kind == Kind::Scatter) {
      for (std::size_t i = 0; i < samples.size(); ++i) indices.push_back(i);
    } else if (!common.empty()) {
      indices.assign(common.begin(), common.end());
    } else {
      indices = SampleIndices(samples, model.x, plot.width,
                              c.kind != Kind::Bar && c.data->kind != DataKind::Category && c.sampling == ChartSampling::Auto);
    }
    // Selection and keyboard focus must remain addressable even when sampling would remove their marks.
    for (const auto& identity : pinned) {
      if (identity.series_key != c.data->entries[s].key) continue;
      const auto found = c.data->entries[s].index->by_key.find(identity.datum_key);
      if (found != c.data->entries[s].index->by_key.end()) {
        auto i = std::find_if(samples.begin(), samples.end(), [&](const ModelSample& p) { return p.index == found->second; });
        if (i != samples.end()) indices.push_back(static_cast<std::size_t>(i - samples.begin()));
      }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    Contour contour{.series = s};
    std::unordered_set<std::uint64_t> representatives;
    std::optional<double> previous_x;
    auto flush = [&] {
      if (!contour.marks.empty()) {
        MakeContour(contour, result, c.interpolation);
        result.contours.push_back(std::move(contour));
        contour = Contour{.series = s};
      }
    };
    for (auto i : indices) {
      const auto& p = samples[i];
      if (p.gap) { flush(); previous_x.reset(); continue; }
      if (c.data->kind == DataKind::Category && previous_x && p.x != *previous_x + 1.0) flush();
      previous_x = p.x;
      const auto projected = ProjectMark(c, model, plot, s, p, markers[s], slot, visible_count);
      if (!projected) continue;
      const auto& mark = *projected;
      if (c.kind == Kind::Scatter && c.scatter_sampling == ScatterSampling::Representative) {
        // Keep an original point per screen-space cell, never an aggregate. Pinned identities bypass reduction.
        const auto cell = Cell(static_cast<int>((mark.point.x - plot.x) / 4.0F),
                               static_cast<int>((mark.point.y - plot.y) / 4.0F));
        const auto identity = DatumInfo(*c.data, mark).identity;
        const bool keep = std::find(pinned.begin(), pinned.end(), identity) != pinned.end();
        if (!representatives.insert(cell).second && !keep) continue;
      }
      contour.marks.push_back(result.marks.size());
      result.marks.push_back(mark);
    }
    if (c.kind == Kind::Line || c.kind == Kind::Area) flush();
    ++slot;
  }
  IndexGeometry(result);
  return result;
}

Geometry InterpolateGeometry(const Geometry& from, const Geometry& to, float progress, ChartInterpolation interpolation) {
  Require(from.marks.size() == to.marks.size(), "Geometry interpolation requires matched marks");
  Geometry result = to;
  // Interpolate in screen space even when numeric domains changed, then reuse the contour builder with
  // identity x mapping and inverted y mapping. Datum indices and values remain those of the target snapshot.
  result.x_domain = {to.plot.x, to.plot.x + to.plot.width};
  result.y_domain = {-to.plot.y - to.plot.height, -to.plot.y};
  for (std::size_t i = 0; i < result.marks.size(); ++i) {
    auto& out = result.marks[i];
    const auto& a = from.marks[i];
    const auto& b = to.marks[i];
    out.point = a.point + (b.point - a.point) * progress;
    out.baseline = a.baseline + (b.baseline - a.baseline) * progress;
    out.source = {out.point.x, -out.point.y};
    out.source_baseline = {out.baseline.x, -out.baseline.y};
    out.area = std::lerp(a.area, b.area, progress);
    // Marker dimensions are nonlinear in area, so recompute bounds instead of interpolating their widths.
    out.bounds = to.kind == Kind::Bar ?
        Rect{std::lerp(a.bounds.x, b.bounds.x, progress), std::lerp(a.bounds.y, b.bounds.y, progress),
             std::lerp(a.bounds.width, b.bounds.width, progress), std::lerp(a.bounds.height, b.bounds.height, progress)} :
        MarkerBounds(out.point, out.area, out.shape);
  }
  for (auto& contour : result.contours) {
    contour.line.Reset();
    contour.fill.Reset();
    contour.segments.clear();
    MakeContour(contour, result, interpolation);
  }
  IndexGeometry(result);
  return result;
}

void RefreshHitIndex(Geometry& geometry) { IndexGeometry(geometry); }

std::optional<std::size_t> Hit(const Geometry& geometry, Point point, float slop) {
  if (!geometry.plot.Contains(point)) return {};
  const auto kind = geometry.kind;
  std::optional<std::size_t> best;
  float best_distance = slop;
  const int cx = static_cast<int>((point.x - geometry.plot.x) / 32.0F);
  const int cy = static_cast<int>((point.y - geometry.plot.y) / 32.0F);
  const int reach = static_cast<int>(std::ceil(slop / 32.0F));
  auto consider = [&](std::size_t index, float distance) {
    // Stable mark order breaks equal-distance ties regardless of which spatial cell supplied a candidate.
    if (distance < best_distance || (distance == best_distance && (!best || index < *best))) {
      best = index;
      best_distance = distance;
    }
  };
  for (int y = cy - reach; y <= cy + reach; ++y) for (int x = cx - reach; x <= cx + reach; ++x) {
    const auto cell = geometry.cells.find(Cell(x, y));
    if (cell == geometry.cells.end()) continue;
    for (auto i : cell->second) {
      const auto& mark = geometry.marks[i];
      if (kind == Kind::Bar) {
        const Point nearest{std::clamp(point.x, mark.bounds.x, mark.bounds.x + mark.bounds.width),
                            std::clamp(point.y, mark.bounds.y, mark.bounds.y + mark.bounds.height)};
        consider(i, Distance(point, nearest));
      } else {
        consider(i, MarkerDistance(mark, point));
      }
    }
  }
  if (kind == Kind::Line || kind == Kind::Area) {
    // Test the rendered, clipped segments rather than source chords, including flattened monotone curves.
    for (const auto& contour : geometry.contours) {
      for (const auto& segment : contour.segments) {
        const auto a = segment.start_mark;
        const auto b = segment.end_mark;
        const auto chosen = Distance(point, geometry.marks[a].point) <= Distance(point, geometry.marks[b].point) ? a : b;
        consider(chosen, SegmentDistance(point, segment.start, segment.end));
      }
      if (kind == Kind::Area && !best && contour.fill.Contains(point, PathFillRule::NonZero)) {
        float distance = std::numeric_limits<float>::infinity();
        for (auto i : contour.marks) {
          const float next = std::abs(point.x - geometry.marks[i].point.x);
          if (next < distance) { best = i; distance = next; }
        }
      }
    }
  }
  return best;
}

} // namespace huxerui::charts_detail
