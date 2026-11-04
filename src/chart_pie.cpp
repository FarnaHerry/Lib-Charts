#include "chart_internal.h"
#include <charts_resources.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace huxerui::charts_detail {
namespace {

constexpr double full_circle = std::numbers::pi_v<double> * 2.0;
constexpr double top_angle = -std::numbers::pi_v<double> * 0.5;
constexpr TweenSpec hover_tween{.duration = 0.18, .easing = Easing::EaseOut};

struct SliceStyle {
  Brush fill;
  Brush stroke;
  StrokeStyle outline{.width = 1.0F, .join = StrokeJoin::Round};
};

void Merge(SliceStyle& target, const PieChartStyle& source) {
  if (source.fill) target.fill = *source.fill;
  if (source.stroke) target.stroke = *source.stroke;
  if (source.stroke_style) target.outline = *source.stroke_style;
}

void Validate(const StrokeStyle& stroke) {
  Require(std::isfinite(stroke.width) && stroke.width >= 0.0F &&
              std::isfinite(stroke.miter_limit) && stroke.miter_limit >= 1.0F &&
              std::isfinite(stroke.dash_offset), "Invalid pie chart stroke style");
  for (const auto dash : stroke.dash_pattern) {
    Require(std::isfinite(dash) && dash >= 0.0F, "Invalid pie chart stroke dash pattern");
  }
}

struct Slice {
  std::string key;
  double share = 0.0;
  double area = 0.0;
  SliceStyle style;
};

struct PiePresentation {
  std::vector<Slice> slices;
  float inner_ratio = 0.0F;
  bool rounded = false;
  bool reduced_motion = false;
  Brush background{Color::Transparent()};
  TextStyle title_style;
  Color text_color;
  Color empty_color;
  std::string title;
  std::string summary;
  float spacing = 0.0F;
};

std::shared_ptr<const PiePresentation> UsePiePresentation(const PieConfig& config) {
  auto out = std::make_shared<PiePresentation>();
  const auto& theme = UseTheme();
  const auto charts = UseEnvironment<ChartTheme>();
  out->rounded = config.style.rounded_caps.value_or(charts.pie.rounded_caps.value_or(false));
  out->inner_ratio = config.style.inner_radius.value_or(charts.pie.inner_radius.value_or(0.0F));
  Require(std::isfinite(out->inner_ratio) && out->inner_ratio >= 0.0F && out->inner_ratio <= 0.9F,
          "Pie inner_radius must be finite and within [0, 0.9]");
  if (out->rounded && out->inner_ratio == 0.0F) out->inner_ratio = 0.62F;
  out->reduced_motion = theme.motion.reduced_motion;
  out->background = config.appearance.background.value_or(charts.common.background.value_or(Brush(Color::Transparent())));
  out->title_style = config.appearance.title.value_or(charts.common.title.value_or(
      TextStyle{Font::System(theme.typography.title_large), theme.colors.on_surface}));
  out->text_color = theme.colors.on_surface;
  out->empty_color = theme.colors.surface_container_high;
  out->empty_color.alpha *= 0.8F;
  out->title = UseString(config.title);
  out->summary = UseString(config.accessibility.summary);
  out->spacing = theme.spacing.small;
  auto palette = charts.palette;
  if (palette.empty()) palette = {theme.colors.primary, theme.colors.secondary, theme.colors.error,
                                 theme.colors.on_surface_variant, theme.colors.outline};

  SliceStyle defaults{.fill = Brush(Color::Transparent()), .stroke = Brush(theme.colors.surface)};
  Merge(defaults, charts.pie);
  Merge(defaults, config.style);
  Validate(defaults.outline);
  const auto data = config.data.Data();
  double maximum = 0.0;
  for (const auto& datum : data) maximum = std::max(maximum, datum.value);
  double total = 0.0;
  // Scaling before summation keeps finite input near DBL_MAX from overflowing.
  if (maximum > 0.0) for (const auto& datum : data) total += datum.value / maximum;
  std::unordered_set<std::string> keys;
  std::string summary;
  out->slices.reserve(data.size());
  for (const auto& datum : data) {
    keys.insert(datum.key);
    SliceStyle style{.fill = Brush(palette[StableHash(datum.key) % palette.size()]),
                     .stroke = Brush(theme.colors.surface)};
    Merge(style, charts.pie);
    Merge(style, config.style);
    if (const auto found = config.slice_styles.find(datum.key); found != config.slice_styles.end()) {
      Merge(style, found->second);
    }
    Validate(style.outline);
    const double share = maximum > 0.0 ? (datum.value / maximum) / total : 0.0;
    const double area = maximum > 0.0 ? share : 1.0 / static_cast<double>(data.size());
    out->slices.push_back({datum.key, share, area, std::move(style)});
    if (!summary.empty()) summary += ", ";
    summary += UseString(datum.label) + " " + std::to_string(static_cast<int>(std::lround(share * 100.0))) + "%";
  }
  for (const auto& [key, style] : config.slice_styles) {
    Require(keys.contains(key), "Unknown pie datum key in SliceStyle");
    Require(!style.inner_radius && !style.rounded_caps, "Pie geometry applies chart-wide, not in SliceStyle");
  }
  if (out->summary.empty()) out->summary = data.empty() ? UseString(::charts::strings::empty) : std::move(summary);
  return out;
}

Point Polar(Point center, float radius, double angle) {
  return {center.x + radius * static_cast<float>(std::cos(angle)),
          center.y + radius * static_cast<float>(std::sin(angle))};
}

void AppendArc(Path& path, Point center, float radius, double start, double sweep) {
  // Two half arcs also represent a complete circle without coincident arc endpoints.
  const int count = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / std::numbers::pi_v<double>)));
  for (int step = 1; step <= count; ++step) {
    path.ArcTo({radius, radius}, 0.0F, ArcSize::Small,
               sweep >= 0.0 ? ArcDirection::Clockwise : ArcDirection::CounterClockwise,
               Polar(center, radius, start + sweep * static_cast<double>(step) / count));
  }
}

Path SectorPath(Point center, float radius, float inner, double start, double sweep, bool whole) {
  Path path;
  path.MoveTo(Polar(center, radius, start));
  AppendArc(path, center, radius, start, sweep);
  if (whole) path.Close();
  if (inner > 0.0F) {
    if (whole) path.MoveTo(Polar(center, inner, start + sweep));
    else path.LineTo(Polar(center, inner, start + sweep));
    // Reverse the inner contour so NonZero filling preserves the donut hole.
    AppendArc(path, center, inner, start + sweep, -sweep);
  } else if (!whole) {
    path.LineTo(center);
  }
  if (!whole || inner > 0.0F) path.Close();
  return path;
}

Color SliceColor(const Brush& brush, Color fallback) {
  return std::visit([&](const auto& source) -> Color {
    if constexpr (std::is_same_v<std::decay_t<decltype(source)>, Color>) return source;
    else {
      if (source.stops.empty()) return fallback;
      // A representative gradient stop keeps the label and halo solid and legible.
      return std::min_element(source.stops.begin(), source.stops.end(), [](const auto& a, const auto& b) {
        return std::abs(a.offset - 0.5F) < std::abs(b.offset - 0.5F);
      })->color;
    }
  }, brush.Get());
}

struct SliceGeometry {
  Path path;
  double start = 0.0;
  double end = 0.0;
  bool whole = false;
};

struct PieSurface {
  std::shared_ptr<const PiePresentation> presentation;
  class Extension;
};

class PieSurface::Extension final : public NodeExtension {
public:
  Extension(ViewNode&, const PieSurface& spec) : spec_(spec), color_from_(P().text_color), color_to_(P().text_color) {
    SyncMotion();
  }

  void Update(ViewNode&, const PieSurface& spec) {
    spec_ = spec;
    SyncMotion();
    dirty_ = true;
    InvalidatePaint(PaintInvalidation::Both);
  }

  PaintInvalidation PrepareGeometry(ViewNode& node, TextMeasurer&) override {
    const auto bounds = node.ContentBounds();
    if (bounds == bounds_ && !dirty_) return PaintInvalidation::None;
    bounds_ = bounds;
    dirty_ = false;
    center_ = {bounds.x + bounds.width * 0.5F, bounds.y + bounds.height * 0.5F};
    const float extent = std::min(bounds.width, bounds.height);
    float outline = 0.0F;
    for (const auto& slice : P().slices) outline = std::max(outline, slice.style.outline.width);
    const float margin = P().rounded ? std::min(10.0F, extent * 0.125F) + outline : outline * 0.5F;
    radius_ = std::max(0.0F, extent * 0.5F - margin);
    inner_ = radius_ * P().inner_ratio;
    width_ = radius_ - inner_;
    mid_ = (radius_ + inner_) * 0.5F;
    geometry_.clear();
    geometry_.resize(P().slices.size());
    const auto positive = std::count_if(P().slices.begin(), P().slices.end(), [](const Slice& s) { return s.area > 0.0; });
    double start = top_angle;
    for (std::size_t i = 0; radius_ > 0.0F && i < P().slices.size(); ++i) {
      const double sweep = P().slices[i].area * full_circle;
      if (sweep <= 0.0) continue;
      auto& arc = geometry_[i];
      arc.whole = positive == 1;
      arc.start = start;
      arc.end = start + sweep;
      if (P().rounded) {
        const double trim = (width_ * 0.5F + 2.0F) / mid_;
        const double inset = arc.whole ? 0.0 : std::min(trim, std::max(0.0, sweep * 0.5 - 0.00005));
        arc.start += inset;
        arc.end -= inset;
        arc.path.MoveTo(Polar(center_, mid_, arc.start));
        AppendArc(arc.path, center_, mid_, arc.start, arc.end - arc.start);
        if (arc.whole) arc.path.Close();
      } else {
        arc.path = SectorPath(center_, radius_, inner_, start, sweep, arc.whole);
      }
      start += sweep;
    }
    if (pointer_) SetHover(node.IsEnabled() ? Hit(*pointer_) : std::nullopt);
    return PaintInvalidation::Both;
  }

  FrameResult OnFrame(ViewNode& node, const FrameInfo& frame) override {
    if (!node.IsEnabled()) { pointer_.reset(); SetHover(std::nullopt); }
    auto policy = frame;
    policy.reduced_motion = policy.reduced_motion || P().reduced_motion;
    auto label = color_progress_.Advance(policy);
    bool changed = label.changed;
    bool running = label.needs_frame;
    for (auto& [key, motion] : motions_) {
      const auto next = motion.Advance(policy);
      changed = changed || next.changed;
      running = running || next.needs_frame;
    }
    if (changed) InvalidatePaint(PaintInvalidation::Both);
    // Geometry is prepared after OnFrame and may retarget hover without a new pointer event.
    const bool rehit = P().rounded && pointer_ && (dirty_ || node.ContentBounds() != bounds_);
    return {.needs_frame = running || rehit};
  }

  bool HoverHitTest(ViewNode&, Point point) const override { return P().rounded && bounds_.Contains(point); }

  void OnHover(ViewNode& node, const HoverEvent& event) override {
    pointer_ = event.type == HoverEventType::Leave ? std::nullopt : std::optional(event.position);
    SetHover(pointer_ && node.IsEnabled() ? Hit(*pointer_) : std::nullopt);
  }

  void PaintBehindContent(const ViewNode&, PaintContext& paint) const override {
    paint.PushClip(bounds_);
    paint.FillPath(Path::RoundedRect(bounds_, {}), P().background);
    if (radius_ > 0.0F) {
      if (P().slices.empty()) paint.FillPath(SectorPath(center_, radius_, inner_, top_angle, full_circle, true), P().empty_color);
      if (P().rounded) {
        // Halos sit behind every slice; stable data order also defines the topmost hit target.
        for (std::size_t i = 0; i < geometry_.size(); ++i) {
          const float progress = motions_.at(P().slices[i].key).Value();
          if (progress <= 0.0F || geometry_[i].path.IsEmpty()) continue;
          for (int layer = 3; layer > 0; --layer) {
            auto color = SliceColor(P().slices[i].style.fill, P().text_color);
            color.alpha *= progress * 0.12F / layer;
            paint.StrokePath(geometry_[i].path, color,
                {.width = StrokeWidth(i) + layer * std::min(4.0F, radius_ * 0.1F),
                 .cap = StrokeCap::Round, .join = StrokeJoin::Round});
          }
        }
      }
      for (std::size_t i = 0; i < geometry_.size(); ++i) {
        const auto& path = geometry_[i].path;
        if (path.IsEmpty()) continue;
        const auto& style = P().slices[i].style;
        if (P().rounded) {
          const float width = StrokeWidth(i);
          if (style.outline.width > 0.0F) paint.StrokePath(path, style.stroke,
              {.width = width + style.outline.width * 2.0F, .cap = StrokeCap::Round, .join = StrokeJoin::Round});
          paint.StrokePath(path, style.fill, {.width = width, .cap = StrokeCap::Round, .join = StrokeJoin::Round});
        } else {
          paint.FillPath(path, style.fill);
          if (style.outline.width > 0.0F) paint.StrokePath(path, style.stroke, style.outline);
        }
      }
      if (P().rounded) {
        const auto active = ActiveSlice();
        const int percent = active ? static_cast<int>(std::lround(P().slices[*active].share * 100.0)) : 0;
        const float font_size = std::min(30.0F, std::min(radius_ * 0.3F, inner_ * 0.65F));
        paint.DrawText({center_.x - inner_, center_.y - font_size, inner_ * 2.0F, font_size * 2.0F},
            std::to_string(percent) + "%", {Font::System(font_size).WithWeight(FontWeight::SemiBold), LabelColor()},
            {.align = TextAlign::Center, .vertical_align = TextVerticalAlign::Center, .wrap = TextWrap::NoWrap});
      }
    }
    paint.PopClip();
  }

private:
  const PiePresentation& P() const { return *spec_.presentation; }

  float StrokeWidth(std::size_t i) const {
    return width_ + 2.0F * std::min(4.0F, width_ * 0.15F) * motions_.at(P().slices[i].key).Value();
  }

  std::optional<std::size_t> Hit(Point point) const {
    if (!P().rounded || radius_ <= 0.0F) return std::nullopt;
    const double distance = std::hypot(point.x - center_.x, point.y - center_.y);
    double angle = std::atan2(point.y - center_.y, point.x - center_.x);
    if (angle < top_angle) angle += full_circle;
    for (std::size_t i = geometry_.size(); i-- > 0;) {
      const auto& arc = geometry_[i];
      if (arc.path.IsEmpty()) continue;
      double nearest = std::numeric_limits<double>::infinity();
      // Consider both sides of the seam when a rounded endpoint crosses twelve o'clock.
      for (const double turn : {-full_circle, 0.0, full_circle}) {
        const double clamped = std::clamp(angle + turn, arc.start, arc.end);
        nearest = std::min(nearest, std::hypot(distance * std::cos(angle) - mid_ * std::cos(clamped),
                                              distance * std::sin(angle) - mid_ * std::sin(clamped)));
      }
      if (nearest <= StrokeWidth(i) * 0.5F) return i;
    }
    return std::nullopt;
  }

  std::optional<std::size_t> ActiveSlice() const {
    std::optional<std::size_t> largest;
    for (std::size_t i = 0; i < P().slices.size(); ++i) {
      if (P().slices[i].area <= 0.0) continue;
      if (hovered_ == P().slices[i].key) return i;
      if (!largest || P().slices[i].share > P().slices[*largest].share) largest = i;
    }
    return largest;
  }

  Color LabelColor() const {
    const float t = color_progress_.Value();
    return {std::lerp(color_from_.red, color_to_.red, t), std::lerp(color_from_.green, color_to_.green, t),
            std::lerp(color_from_.blue, color_to_.blue, t), std::lerp(color_from_.alpha, color_to_.alpha, t)};
  }

  void SyncMotion() {
    std::unordered_set<std::string> keys;
    bool found_hover = false;
    for (const auto& slice : P().slices) {
      keys.insert(slice.key);
      motions_.try_emplace(slice.key, 0.0F);
      found_hover = found_hover || (slice.area > 0.0 && hovered_ == slice.key);
    }
    std::erase_if(motions_, [&](const auto& item) { return !keys.contains(item.first); });
    if (!P().rounded || !found_hover) hovered_.reset();
    TargetMotion();
  }

  void SetHover(std::optional<std::size_t> index) {
    const auto key = index ? std::optional(P().slices[*index].key) : std::nullopt;
    if (hovered_ == key) return;
    hovered_ = key;
    TargetMotion();
    InvalidatePaint(PaintInvalidation::Both);
  }

  void TargetMotion() {
    Color target = P().text_color;
    for (const auto& slice : P().slices) {
      auto& motion = motions_.at(slice.key);
      const bool active = hovered_ == slice.key;
      if (!P().rounded) motion.Set(0.0F);
      else if (motion.Target() != (active ? 1.0F : 0.0F)) motion.AnimateTo(active ? 1.0F : 0.0F, hover_tween);
      if (active) target = SliceColor(slice.style.fill, target);
    }
    if (target != color_to_) {
      color_from_ = LabelColor();
      color_to_ = target;
      color_progress_.Set(0.0F);
      color_progress_.AnimateTo(1.0F, hover_tween);
    }
  }

  PieSurface spec_;
  Rect bounds_;
  Point center_;
  float radius_ = 0.0F;
  float inner_ = 0.0F;
  float mid_ = 0.0F;
  float width_ = 0.0F;
  bool dirty_ = true;
  std::vector<SliceGeometry> geometry_;
  std::optional<Point> pointer_;
  std::optional<std::string> hovered_;
  std::unordered_map<std::string, MotionController> motions_;
  MotionController color_progress_{1.0F};
  Color color_from_;
  Color color_to_;
};

} // namespace

[[huxerui::composable]] View BuildPieChart(std::shared_ptr<const PieConfig> config) {
  const auto presentation = UsePiePresentation(*config);
  View plot = Canvas([](PaintContext&, Size) {})
      .With(PieSurface{presentation}, Grow(), Semantics{.role = SemanticRole::Image, .label = presentation->summary});
  if (presentation->title.empty()) return plot;
  return Column {
    Text(presentation->title, TextRole::Title).Style(presentation->title_style),
    plot,
  }.With(Spacing(presentation->spacing), CrossAlign(CrossAxisAlignment::Stretch));
}

} // namespace huxerui::charts_detail
