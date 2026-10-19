#include "chart_internal.h"
#include <charts_resources.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <memory>
#include <unordered_set>

namespace huxerui::charts_detail {
namespace {

struct ResolvedSliceStyle {
  Brush fill;
  Brush stroke;
  StrokeStyle stroke_style{.width = 1.0F, .join = StrokeJoin::Round};
  std::optional<float> inner_radius;
};

void Merge(ResolvedSliceStyle& target, const PieChartStyle& source) {
  if (source.fill) target.fill = *source.fill;
  if (source.stroke) target.stroke = *source.stroke;
  if (source.stroke_style) target.stroke_style = *source.stroke_style;
  if (source.inner_radius && std::isfinite(*source.inner_radius)) {
    target.inner_radius = *source.inner_radius;
  }
}

void Merge(ChartStyle& target, const ChartStyle& source) {
  if (source.background) target.background = source.background;
  if (source.title) target.title = source.title;
}

void Validate(const StrokeStyle& stroke) {
  Require(std::isfinite(stroke.width) && stroke.width >= 0.0F &&
              std::isfinite(stroke.miter_limit) && stroke.miter_limit >= 1.0F &&
              std::isfinite(stroke.dash_offset),
          "Invalid pie chart stroke style");
  for (const auto dash : stroke.dash_pattern) {
    Require(std::isfinite(dash) && dash >= 0.0F, "Invalid pie chart stroke dash pattern");
  }
}

std::vector<double> Shares(std::span<const PieDatum> data) {
  double maximum = 0.0;
  for (const auto& datum : data) maximum = std::max(maximum, datum.value);
  if (maximum == 0.0) return std::vector<double>(data.size(), 0.0);

  double scaled_total = 0.0;
  for (const auto& datum : data) scaled_total += datum.value / maximum;
  std::vector<double> shares;
  shares.reserve(data.size());
  for (const auto& datum : data) shares.push_back((datum.value / maximum) / scaled_total);
  return shares;
}

std::string Summary(std::span<const PieDatum> data, std::span<const std::string> labels,
                    std::span<const double> shares, const std::string& empty_label) {
  if (std::none_of(data.begin(), data.end(), [](const PieDatum& datum) { return datum.value > 0.0; })) {
    return empty_label;
  }

  std::string summary;
  for (std::size_t index = 0; index < data.size(); ++index) {
    if (data[index].value <= 0.0) continue;
    if (!summary.empty()) summary += ", ";
    summary += labels[index];
    summary += " ";
    summary += std::to_string(static_cast<int>(std::lround(shares[index] * 100.0)));
    summary += "%";
  }
  return summary;
}

constexpr double full_circle = std::numbers::pi_v<double> * 2.0;
constexpr double top_angle = -std::numbers::pi_v<double> * 0.5;

struct RingGeometry {
  Point center;
  float radius = 0.0F;
  float inner_radius = 0.0F;
  float mid_radius = 0.0F;
  float width = 0.0F;
  double trim = 0.0;
};

RingGeometry RoundedGeometry(Size size, float inner_ratio) {
  RingGeometry result;
  result.center = {size.width * 0.5F, size.height * 0.5F};
  const float extent = std::min(size.width, size.height);
  // Keep the expanded stroke and its halo inside the canvas.
  const float hover_margin = std::min(10.0F, extent * 0.125F);
  result.radius = std::max(0.0F, extent * 0.5F - 1.0F - hover_margin);
  result.inner_radius = result.radius * inner_ratio;
  result.width = result.radius - result.inner_radius;
  result.mid_radius = (result.radius + result.inner_radius) * 0.5F;
  if (result.mid_radius > 0.0F) {
    // Reserve half a stroke at each end plus a small visible space between adjacent caps.
    result.trim = (result.width * 0.5F + 2.0F) / result.mid_radius;
  }
  return result;
}

float HoverGrowth(const RingGeometry& ring) {
  return std::min(4.0F, ring.width * 0.15F);
}

Color SliceColor(const Brush& brush, Color fallback) {
  if (const auto* solid = std::get_if<Color>(&brush.Get())) return *solid;
  const auto* linear = std::get_if<LinearGradient>(&brush.Get());
  const auto& stops = linear ? linear->stops : std::get<RadialGradient>(brush.Get()).stops;
  if (stops.empty()) return fallback;
  // Use the gradient's middle color for the solid center label and halo.
  const auto upper = std::upper_bound(stops.begin(), stops.end(), 0.5F,
      [](float offset, const GradientStop& stop) { return offset < stop.offset; });
  if (upper == stops.begin()) return upper->color;
  if (upper == stops.end()) return stops.back().color;
  const auto& lower = *(upper - 1);
  const float t = (0.5F - lower.offset) / (upper->offset - lower.offset);
  return {std::lerp(lower.color.red, upper->color.red, t),
          std::lerp(lower.color.green, upper->color.green, t),
          std::lerp(lower.color.blue, upper->color.blue, t),
          std::lerp(lower.color.alpha, upper->color.alpha, t)};
}

Color BlendColor(Color from, Color to, float progress) {
  return {std::lerp(from.red, to.red, progress), std::lerp(from.green, to.green, progress),
          std::lerp(from.blue, to.blue, progress), std::lerp(from.alpha, to.alpha, progress)};
}

constexpr TweenSpec hover_tween{.duration = 0.18, .easing = Easing::EaseOut};

struct PieHoverState {
  std::unordered_map<std::string, MotionController> arcs;
  MotionController color_progress{1.0F};
  Color color_from;
  Color color_to;
  bool initialized = false;

  Color LabelColor() const { return BlendColor(color_from, color_to, color_progress.Value()); }

  void TargetColor(Color color) {
    if (!initialized) {
      color_from = color_to = color;
      initialized = true;
    } else if (color_to != color) {
      // Retarget from the displayed color so quick movement never restarts from an old endpoint.
      color_from = LabelColor();
      color_to = color;
      color_progress.Set(0.0F);
      color_progress.AnimateTo(1.0F, hover_tween);
    }
  }
};

struct PieHoverAnimation {
  std::shared_ptr<PieHoverState> state;
  bool reduced_motion = false;
  class Extension;
};

class PieHoverAnimation::Extension final : public NodeExtension {
public:
  Extension(ViewNode&, const PieHoverAnimation& spec) : spec_(spec) {}
  void Update(ViewNode&, const PieHoverAnimation& spec) { spec_ = spec; }

  FrameResult OnFrame(ViewNode&, const FrameInfo& frame) override {
    auto policy = frame;
    policy.reduced_motion = policy.reduced_motion || spec_.reduced_motion;
    const auto label = spec_.state->color_progress.Advance(policy);
    bool changed = label.changed;
    bool needs_frame = label.needs_frame;
    for (auto& [key, motion] : spec_.state->arcs) {
      const auto advance = motion.Advance(policy);
      changed = changed || advance.changed;
      needs_frame = needs_frame || advance.needs_frame;
    }
    if (changed) InvalidatePaint(PaintInvalidation::Both);
    return {.needs_frame = needs_frame};
  }

private:
  PieHoverAnimation spec_;
};

std::pair<double, double> RoundedAngles(double start, double sweep, double trim, bool whole) {
  if (whole) return {start, start + sweep};
  // Keep even very small positive slices drawable as a rounded dot.
  const double inset = std::min(trim, std::max(0.0, sweep * 0.5 - 0.00005));
  return {start + inset, start + sweep - inset};
}

Path ArcPath(const RingGeometry& ring, double start, double end, bool whole) {
  Path path;
  const double sweep = end - start;
  const int steps = std::clamp(static_cast<int>(std::ceil(sweep * ring.mid_radius / 3.0)), 1, 512);
  for (int step = 0; step <= steps; ++step) {
    const double angle = start + sweep * static_cast<double>(step) / static_cast<double>(steps);
    Point point{ring.center.x + ring.mid_radius * static_cast<float>(std::cos(angle)),
                ring.center.y + ring.mid_radius * static_cast<float>(std::sin(angle))};
    if (step == 0) path.MoveTo(point); else path.LineTo(point);
  }
  if (whole) path.Close();
  return path;
}

std::optional<std::size_t> HitRoundedRing(Point point, Size size, std::span<const double> shares,
                                          float inner_ratio, std::optional<std::size_t> hovered) {
  const auto ring = RoundedGeometry(size, inner_ratio);
  if (ring.radius <= 0.0F) return std::nullopt;
  const double dx = static_cast<double>(point.x - ring.center.x);
  const double dy = static_cast<double>(point.y - ring.center.y);
  const double distance = std::hypot(dx, dy);
  double angle = std::atan2(dy, dx) - top_angle;
  if (angle < 0.0) angle += full_circle;
  double start = 0.0;
  const auto count = static_cast<std::size_t>(std::count_if(shares.begin(), shares.end(),
      [](double share) { return share > 0.0; }));
  bool in_hover_extension = false;
  for (std::size_t index = 0; index < shares.size(); ++index) {
    const double sweep = shares[index] * full_circle;
    if (sweep <= 0.0) continue;
    const auto [from, to] = RoundedAngles(start, sweep, ring.trim, count == 1);
    const double clamped = std::clamp(angle, from, to);
    const double arc_distance = std::hypot(
        distance * std::cos(angle) - ring.mid_radius * std::cos(clamped),
        distance * std::sin(angle) - ring.mid_radius * std::sin(clamped));
    if (arc_distance <= ring.width * 0.5F) return index;
    if (hovered == index && arc_distance <= ring.width * 0.5F + HoverGrowth(ring)) {
      in_hover_extension = true;
    }
    start += sweep;
  }
  // Give another slice's original shape priority over the active slice's expansion.
  return in_hover_extension ? hovered : std::nullopt;
}

Path SectorPath(Point center, float radius, float inner_radius, double start,
                double sweep, bool whole_circle) {
  const auto steps = std::clamp(
      static_cast<int>(std::ceil(sweep * static_cast<double>(radius) / 3.0)), 2, 512);
  const auto point = [&](float r, double angle) {
    return Point{center.x + r * static_cast<float>(std::cos(angle)),
                 center.y + r * static_cast<float>(std::sin(angle))};
  };
  Path path;
  // inner_radius > 0 时渲染为环形段：外弧正向 + 内弧反向，NonZero 填充出孔。
  const bool donut = inner_radius > 0.0F && inner_radius < radius;
  if (whole_circle) {
    if (!donut) {
      path.MoveTo(point(radius, start));
      for (int step = 1; step < steps; ++step) {
        const double current = start + sweep * static_cast<double>(step) / static_cast<double>(steps);
        path.LineTo(point(radius, current));
      }
      path.Close();
      return path;
    }
    path.MoveTo(point(radius, start));
    for (int step = 1; step <= steps; ++step) {
      const double current = start + sweep * static_cast<double>(step) / static_cast<double>(steps);
      path.LineTo(point(radius, current));
    }
    path.Close();
    path.MoveTo(point(inner_radius, start + sweep));
    for (int step = 1; step <= steps; ++step) {
      const double current = start + sweep - sweep * static_cast<double>(step) / static_cast<double>(steps);
      path.LineTo(point(inner_radius, current));
    }
    path.Close();
    return path;
  }
  if (!donut) {
    path.MoveTo(center);
    for (int step = 0; step <= steps; ++step) {
      const double current = start + sweep * static_cast<double>(step) / static_cast<double>(steps);
      path.LineTo(point(radius, current));
    }
    path.Close();
    return path;
  }
  path.MoveTo(point(radius, start));
  for (int step = 1; step <= steps; ++step) {
    const double current = start + sweep * static_cast<double>(step) / static_cast<double>(steps);
    path.LineTo(point(radius, current));
  }
  path.LineTo(point(inner_radius, start + sweep));
  for (int step = 1; step <= steps; ++step) {
    const double current = start + sweep - sweep * static_cast<double>(step) / static_cast<double>(steps);
    path.LineTo(point(inner_radius, current));
  }
  path.Close();
  return path;
}

void PaintPie(PaintContext& context, Size size, std::span<const double> shares,
              std::span<const ResolvedSliceStyle> styles, Color empty_color, bool rounded_caps,
              std::optional<std::size_t> hovered, Color center_color, std::span<const float> highlights) {
  const Point center{size.width * 0.5F, size.height * 0.5F};
  // 圆环内径取各切片解析值里的最大者（chart-wide Style 会合并进每个切片），
  // 限幅到外径的 90%，避免内径逼近外径后段路径退化。
  float inner_ratio = 0.0F;
  for (const auto& style : styles) {
    if (!style.inner_radius) continue;
    inner_ratio = std::max(inner_ratio, std::clamp(*style.inner_radius, 0.0F, 0.9F));
  }
  if (rounded_caps && inner_ratio == 0.0F) inner_ratio = 0.62F;
  const auto ring = RoundedGeometry(size, inner_ratio);
  const float radius = rounded_caps ? ring.radius
      : std::max(0.0F, std::min(size.width, size.height) * 0.5F - 1.0F);
  if (radius <= 0.0F) return;
  const float inner_radius = inner_ratio * radius;
  const std::size_t positive_count = static_cast<std::size_t>(std::count_if(
      shares.begin(), shares.end(), [](double share) { return share > 0.0; }));
  if (positive_count == 0) {
    if (inner_radius > 0.0F) {
      context.FillPath(SectorPath(center, radius, inner_radius,
                                  -std::numbers::pi_v<double> * 0.5,
                                  std::numbers::pi_v<double> * 2.0, true),
                       empty_color);
    } else {
      context.DrawCircle(center, radius, empty_color);
    }
    if (!rounded_caps) return;
  } else if (rounded_caps) {
    const auto paint_segment = [&](const Path& path, std::size_t index, float highlight) {
      const float width = ring.width + HoverGrowth(ring) * 2.0F * highlight;
      if (highlight > 0.0F) {
        const Color color = SliceColor(styles[index].fill, center_color);
        for (int layer = 3; layer > 0; --layer) {
          auto halo = color;
          halo.alpha *= highlight * 0.12F / static_cast<float>(layer);
          context.StrokePath(path, halo,
              StrokeStyle{.width = width + static_cast<float>(layer) * std::min(4.0F, ring.radius * 0.1F),
                          .cap = StrokeCap::Round, .join = StrokeJoin::Round});
        }
      }
      if (styles[index].stroke_style.width > 0.0F) {
        context.StrokePath(path, styles[index].stroke,
            StrokeStyle{.width = width + styles[index].stroke_style.width * 2.0F,
                        .cap = StrokeCap::Round, .join = StrokeJoin::Round});
      }
      context.StrokePath(path, styles[index].fill,
          StrokeStyle{.width = width, .cap = StrokeCap::Round, .join = StrokeJoin::Round});
    };
    std::vector<std::pair<std::size_t, Path>> highlighted_paths;
    double start = top_angle;
    for (std::size_t index = 0; index < shares.size(); ++index) {
      const double sweep = shares[index] * full_circle;
      if (sweep <= 0.0) continue;
      const auto [from, to] = RoundedAngles(start, sweep, ring.trim, positive_count == 1);
      const auto path = ArcPath(ring, from, to, positive_count == 1);
      if (highlights[index] > 0.0F) highlighted_paths.emplace_back(index, path);
      else paint_segment(path, index, 0.0F);
      start += sweep;
    }
    std::stable_sort(highlighted_paths.begin(), highlighted_paths.end(), [&](const auto& a, const auto& b) {
      return highlights[a.first] < highlights[b.first];
    });
    for (const auto& [index, path] : highlighted_paths) paint_segment(path, index, highlights[index]);
  }
  if (rounded_caps) {
    std::size_t active = shares.size();
    if (hovered && *hovered < shares.size() && shares[*hovered] > 0.0) active = *hovered;
    if (active == shares.size() && positive_count > 0) {
      active = static_cast<std::size_t>(std::distance(shares.begin(),
          std::max_element(shares.begin(), shares.end())));
    }
    const int percent = active == shares.size() ? 0 : static_cast<int>(std::lround(shares[active] * 100.0));
    const float font_size = std::clamp(radius * 0.3F, 11.0F, 30.0F);
    context.DrawText({center.x - inner_radius, center.y - font_size, inner_radius * 2.0F, font_size * 2.0F},
        std::to_string(percent) + "%",
        {Font::System(font_size).WithWeight(FontWeight::SemiBold), center_color},
        {.align = TextAlign::Center, .vertical_align = TextVerticalAlign::Center, .wrap = TextWrap::NoWrap});
    return;
  }

  double start = -std::numbers::pi_v<double> * 0.5;
  for (std::size_t index = 0; index < shares.size(); ++index) {
    const double sweep = shares[index] * 2.0 * std::numbers::pi_v<double>;
    if (sweep <= 0.0) continue;
    const auto path = SectorPath(center, radius, inner_radius, start, sweep,
                                 positive_count == 1);
    context.FillPath(path, styles[index].fill);
    if (styles[index].stroke_style.width > 0.0F) {
      context.StrokePath(path, styles[index].stroke, styles[index].stroke_style);
    }
    start += sweep;
  }
}

} // namespace

[[huxerui::composable]] View BuildPieChart(std::shared_ptr<const PieConfig> config) {
  const auto& theme = UseTheme();
  const auto charts = UseEnvironment<ChartTheme>();
  const auto data = config->data.Data();
  const auto shares = Shares(data);
  std::vector<std::string> labels;
  labels.reserve(data.size());
  for (const auto& datum : data) labels.push_back(UseString(datum.label));

  auto palette = charts.palette;
  if (palette.empty()) {
    palette = {theme.colors.primary, theme.colors.secondary, theme.colors.error,
               theme.colors.on_surface_variant, theme.colors.outline};
  }

  std::unordered_set<std::string> keys;
  keys.reserve(data.size());
  std::vector<ResolvedSliceStyle> styles;
  styles.reserve(data.size());
  for (const auto& datum : data) {
    keys.insert(datum.key);
    ResolvedSliceStyle style{
        .fill = Brush(palette[StableHash(datum.key) % palette.size()]),
        .stroke = Brush(theme.colors.surface),
    };
    Merge(style, charts.pie);
    Merge(style, config->style);
    if (const auto found = config->slice_styles.find(datum.key); found != config->slice_styles.end()) {
      Merge(style, found->second);
    }
    Validate(style.stroke_style);
    styles.push_back(std::move(style));
  }
  for (const auto& [key, style] : config->slice_styles) {
    Require(keys.contains(key), "Unknown pie datum key in SliceStyle");
    Require(!style.rounded_caps.has_value(), "Pie rounded_caps applies chart-wide, not in SliceStyle");
    Validate(style.stroke_style.value_or(StrokeStyle{.width = 1.0F, .join = StrokeJoin::Round}));
  }

  const bool rounded_caps = config->style.rounded_caps.value_or(charts.pie.rounded_caps.value_or(false));
  float inner_ratio = 0.0F;
  for (const auto& style : styles) {
    if (style.inner_radius) inner_ratio = std::max(inner_ratio, std::clamp(*style.inner_radius, 0.0F, 0.9F));
  }
  if (rounded_caps && inner_ratio == 0.0F) inner_ratio = 0.62F;
  const auto hovered_key = UseState(std::optional<std::string>{});
  std::optional<std::size_t> hovered;
  if (rounded_caps && hovered_key.Get()) {
    for (std::size_t index = 0; index < data.size(); ++index) {
      if (data[index].key == *hovered_key.Get()) { hovered = index; break; }
    }
  }

  ChartStyle appearance{
      .background = Brush(Color::Transparent()),
      .title = TextStyle{Font::System(theme.typography.title_large), theme.colors.on_surface},
  };
  Merge(appearance, charts.common);
  Merge(appearance, config->appearance);

  Color empty_color = theme.colors.surface_container_high;
  empty_color.alpha *= 0.8F;
  const std::string empty_label = UseString(::charts::strings::empty);
  const std::string custom_summary = UseString(config->accessibility.summary);
  const std::string summary = custom_summary.empty()
      ? Summary(data, labels, shares, empty_label)
      : custom_summary;
  const std::string title = UseString(config->title);
  const auto painted_size = UseState(std::make_shared<Size>()).Get();
  const auto hover_motion = UseState(std::make_shared<PieHoverState>()).Get();
  std::erase_if(hover_motion->arcs, [&](const auto& entry) { return !keys.contains(entry.first); });
  std::vector<std::string> datum_keys;
  datum_keys.reserve(data.size());
  for (std::size_t index = 0; index < data.size(); ++index) {
    datum_keys.push_back(data[index].key);
    auto& motion = hover_motion->arcs.try_emplace(data[index].key, 0.0F).first->second;
    const float target = rounded_caps && hovered == index ? 1.0F : 0.0F;
    if (!rounded_caps) motion.Set(0.0F);
    else if (motion.Target() != target) motion.AnimateTo(target, hover_tween);
  }
  hover_motion->TargetColor(hovered && shares[*hovered] > 0.0
      ? SliceColor(styles[*hovered].fill, theme.colors.on_surface) : theme.colors.on_surface);
  auto plot = Canvas([shares, styles, empty_color, rounded_caps, hovered,
                      background = appearance.background, hover_motion, datum_keys,
                      painted_size](PaintContext& context, Size size) {
    *painted_size = size;
    if (background) context.FillPath(Path::RoundedRect({0.0F, 0.0F, size.width, size.height}, {}), *background);
    std::vector<float> highlights;
    highlights.reserve(datum_keys.size());
    for (const auto& key : datum_keys) highlights.push_back(hover_motion->arcs.at(key).Value());
    PaintPie(context, size, shares, styles, empty_color, rounded_caps, hovered,
             hover_motion->LabelColor(), highlights);
  }).With(Semantics{.role = SemanticRole::Image, .label = summary});
  if (rounded_caps) {
    plot = std::move(plot).With(PieHoverAnimation{hover_motion, theme.motion.reduced_motion}).On<ViewEvents::Hover>(
        [shares, inner_ratio, painted_size, datum_keys = std::move(datum_keys), hovered_key, hovered](const HoverEvent& event) {
          const auto index = event.type == HoverEventType::Leave ? std::nullopt
              : HitRoundedRing(event.position, *painted_size, shares, inner_ratio, hovered);
          const auto key = index ? std::optional(datum_keys[*index]) : std::nullopt;
          if (hovered_key.Get() != key) hovered_key = key;
        });
  }
  if (title.empty()) return plot;
  return Column {
    Text(title).Style(*appearance.title),
    plot,
  }.With(Spacing(theme.spacing.small), CrossAlign(CrossAxisAlignment::Stretch));
}

} // namespace huxerui::charts_detail
