#include "chart_internal.h"
#include <charts_resources.h>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace huxerui::charts_detail {
namespace {

// Absence means inheritance; explicit false, zero and transparent values must still override earlier layers.
template <class T> void Override(std::optional<T>& target, const std::optional<T>& source) {
  if (source) target = source;
}
void Merge(ChartMarkerStyle& a, const ChartMarkerStyle& b) {
  Override(a.shape, b.shape);
  Override(a.visible, b.visible);
  Override(a.area, b.area);
  Override(a.fill, b.fill);
  Override(a.stroke, b.stroke);
  Override(a.stroke_style, b.stroke_style);
}
void Merge(ChartStyle& a, const ChartStyle& b) {
  Override(a.background, b.background);
  Override(a.title, b.title);
  Override(a.axis.line, b.axis.line);
  Override(a.axis.grid, b.axis.grid);
  Override(a.axis.text, b.axis.text);
  Override(a.axis.width, b.axis.width);
  Override(a.legend.text, b.legend.text);
  Override(a.legend.spacing, b.legend.spacing);
  Override(a.interaction.highlight, b.interaction.highlight);
  Override(a.interaction.width, b.interaction.width);
  Override(a.interaction.hit_slop, b.interaction.hit_slop);
}

struct MarkStyle {
  Brush fill = Color::Transparent();
  Brush stroke = Color::Transparent();
  StrokeStyle stroke_style{.width = 2.0F, .cap = StrokeCap::Round, .join = StrokeJoin::Round};
  ChartMarkerStyle marker;
  float corner_radius = 3.0F;
  bool operator==(const MarkStyle&) const = default;
};

void Merge(MarkStyle& out, const SeriesStyle& style) {
  std::visit([&](const auto& value) {
    if constexpr (requires { value.fill; }) if (value.fill) out.fill = *value.fill;
    if constexpr (requires { value.stroke; }) if (value.stroke) out.stroke = *value.stroke;
    if constexpr (requires { value.stroke_style; }) if (value.stroke_style) out.stroke_style = *value.stroke_style;
    if constexpr (requires { value.marker; }) Merge(out.marker, value.marker);
    if constexpr (requires { value.corner_radius; }) if (value.corner_radius) out.corner_radius = *value.corner_radius;
  }, style);
}

void Nonnegative(float value) { Require(std::isfinite(value) && value >= 0.0F, "Chart style dimensions must be finite and nonnegative"); }
void ValidateStroke(const StrokeStyle& style) {
  Nonnegative(style.width);
  Require(std::isfinite(style.miter_limit) && style.miter_limit >= 1.0F && std::isfinite(style.dash_offset), "Invalid chart stroke style");
  for (auto value : style.dash_pattern) Nonnegative(value);
}

std::string IdentityKey(const ChartDatumIdentity& identity) {
  // Prefix the first key's length so arbitrary separators inside either key cannot make two pairs ambiguous.
  return std::to_string(identity.series_key.size()) + ":" + identity.series_key + identity.datum_key;
}

Brush WithOpacity(const Brush& brush, float opacity) {
  return std::visit([&](auto source) -> Brush {
    if constexpr (std::is_same_v<decltype(source), Color>) source.alpha *= opacity;
    else for (auto& stop : source.stops) stop.color.alpha *= opacity;
    return source;
  }, brush.Get());
}

bool SameMarkerGeometry(const std::vector<ChartMarkerStyle>& a, const std::vector<ChartMarkerStyle>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].area != b[i].area || a[i].shape != b[i].shape || a[i].visible != b[i].visible) return false;
  }
  return true;
}

// Own resolved theme values and localized strings beyond the composition call. Retained callbacks must not
// borrow Environment references or resolve composition-bound resources while painting or handling input.
struct Presentation {
  std::shared_ptr<const Config> config;
  ChartStyle style;
  std::vector<MarkStyle> marks;
  std::vector<ChartMarkerStyle> markers;
  std::string title;
  std::string summary;
  std::string x_title;
  std::string y_title;
  std::string empty_label;
  std::string count_label;
  std::string representative_label;
  std::vector<std::string> series_labels;
  std::vector<std::string> categories;
  std::vector<std::optional<std::string>> x_tick_labels;
  std::vector<std::optional<std::string>> y_tick_labels;
  TextShapingOptions shaping;
  TooltipStyle tooltip;
  Color surface;
  bool reduced_motion = false;
};

std::shared_ptr<const Presentation> UsePresentation(std::shared_ptr<const Config> config) {
  auto out = std::make_shared<Presentation>();
  out->config = std::move(config);
  const auto& c = *out->config;
  const auto theme = UseTheme();
  const auto charts = UseEnvironment<ChartTheme>();
  out->surface = theme.colors.surface;
  out->style = ChartStyle{
      .background = Brush(Color::Transparent()),
      .title = TextStyle{Font::System(theme.typography.title_large), theme.colors.on_surface},
      .axis = {.line = theme.colors.outline, .grid = theme.colors.surface_container_highest,
               .text = TextStyle{Font::System(theme.typography.body_small), theme.colors.on_surface_variant}, .width = 1.0F},
      .legend = {.text = TextStyle{Font::System(theme.typography.body_small), theme.colors.on_surface}, .spacing = theme.spacing.small},
      .interaction = {.highlight = theme.colors.primary, .width = 2.0F, .hit_slop = 8.0F},
  };
  Merge(out->style, charts.common);
  Merge(out->style, c.appearance);
  Nonnegative(*out->style.axis.width);
  Nonnegative(*out->style.legend.spacing);
  Nonnegative(*out->style.interaction.width);
  Nonnegative(*out->style.interaction.hit_slop);
  Require(*out->style.interaction.hit_slop <= 128.0F, "Chart hit slop cannot exceed 128 DIPs");
  out->title = UseString(c.title);
  out->summary = UseString(c.accessibility.summary);
  out->x_title = UseString(Access::Title(c.x));
  out->y_title = UseString(Access::Title(c.y));
  out->shaping.locale = std::string(UseEnvironment<Locale>().LanguageTag());
  out->tooltip = UseEnvironment<TooltipStyle>();
  out->reduced_motion = theme.motion.reduced_motion;
  out->empty_label = UseString(::charts::strings::empty);
  out->count_label = UseString(::charts::strings::count);
  out->representative_label = UseString(::charts::strings::representative);
  auto palette = charts.palette;
  if (palette.empty()) palette = {theme.colors.primary, theme.colors.secondary, theme.colors.error,
                                 theme.colors.on_surface_variant, theme.colors.outline};
  for (const auto& e : c.data->entries) {
    out->series_labels.push_back(UseString(e.label));
    const auto color = palette[StableHash(e.key) % palette.size()];
    auto area_color = color;
    area_color.alpha *= 0.28F;
    MarkStyle style{.fill = c.kind == Kind::Area ? Brush(area_color) : Brush(color), .stroke = color};
    style.marker.shape = static_cast<ChartMarkerShape>((StableHash(e.key) / palette.size()) % 5);
    style.marker.visible = c.kind == Kind::Scatter;
    style.marker.area = 36.0F;
    style.marker.stroke_style = StrokeStyle{.width = 1.0F};
    // Apply theme, component and keyed overrides in order; the plot and legend consume this same final style.
    switch (c.kind) {
      case Kind::Line: Merge(style, charts.line); break;
      case Kind::Area: Merge(style, charts.area); break;
      case Kind::Bar: Merge(style, charts.bar); break;
      case Kind::Scatter: Merge(style, charts.scatter); break;
    }
    Merge(style, c.style);
    if (auto found = c.series_styles.find(e.key); found != c.series_styles.end()) Merge(style, found->second);
    // Derive unspecified marker brushes last so they follow any overridden series fill or stroke.
    if (!style.marker.fill) style.marker.fill = c.kind == Kind::Line ? style.stroke : style.fill;
    if (!style.marker.stroke) style.marker.stroke = style.stroke;
    Nonnegative(*style.marker.area);
    Nonnegative(style.corner_radius);
    ValidateStroke(style.stroke_style);
    ValidateStroke(*style.marker.stroke_style);
    out->markers.push_back(style.marker);
    out->marks.push_back(std::move(style));
  }
  for (const auto& category : *c.data->categories) out->categories.push_back(UseString(category.label));
  for (const auto& tick : Access::Ticks(c.x)) out->x_tick_labels.push_back(tick.label ? std::optional(UseString(*tick.label)) : std::nullopt);
  for (const auto& tick : Access::Ticks(c.y)) out->y_tick_labels.push_back(tick.label ? std::optional(UseString(*tick.label)) : std::nullopt);
  return out;
}

struct TickLayout {
  double value = 0.0;
  std::string text;
  TextRunMetrics metrics;
  float position = 0.0F;
  bool show_label = true;
};

std::string Format(const ChartAxis& axis, double value) {
  const auto& formatter = Access::Formatter(axis);
  return formatter ? (*formatter)(value) : Number(value);
}

std::vector<TickLayout> Ticks(const ChartAxis& axis, ChartNumericDomain domain, float length,
                            const Presentation& p, bool x, TextMeasurer& measurer) {
  // Tick count is a density hint. PrepareGeometry performs final label collision checks using measured text.
  std::vector<TickLayout> result;
  if (!Access::Visible(axis)) return result;
  const auto& explicit_labels = x ? p.x_tick_labels : p.y_tick_labels;
  if (Access::Category(axis)) {
    const auto count = p.categories.size();
    const auto budget = std::max<std::size_t>(2, std::min<std::size_t>(Access::Count(axis), static_cast<std::size_t>(std::max(2.0F, length / 32.0F))));
    const auto stride = std::max<std::size_t>(1, (count + budget - 1) / budget);
    for (std::size_t i = 0; i < count; i += stride) result.push_back({static_cast<double>(i), p.categories[i]});
  } else if (!Access::Ticks(axis).empty()) {
    const auto& ticks = Access::Ticks(axis);
    for (std::size_t i = 0; i < ticks.size(); ++i) if (ticks[i].value >= domain.min && ticks[i].value <= domain.max) {
      result.push_back({ticks[i].value, explicit_labels[i].value_or(Format(axis, ticks[i].value))});
    }
  } else {
    const auto count = std::max<std::size_t>(2, std::min<std::size_t>(Access::Count(axis), static_cast<std::size_t>(std::max(2.0F, length / 64.0F))));
    for (std::size_t i = 0; i < count; ++i) {
      const auto value = std::lerp(domain.min, domain.max, static_cast<double>(i) / static_cast<double>(count - 1));
      result.push_back({value, Format(axis, value)});
    }
  }
  for (auto& tick : result) tick.metrics = measurer.MeasureRun(tick.text, *p.style.axis.text, p.shaping);
  return result;
}

struct ChartSurface {
  std::shared_ptr<const Presentation> presentation;
  PopupHandle popup;
  class Extension;
};

// Keep the painted bar and its interaction outline identical, including square internal stack boundaries.
Path BarPath(const Mark& mark, ChartOrientation orientation, float radius) {
  CornerRadii corners;
  if (mark.exposed) {
    if (orientation == ChartOrientation::Horizontal) corners = mark.point.x >= mark.baseline.x ?
        CornerRadii{0.0F, radius, radius, 0.0F} : CornerRadii{radius, 0.0F, 0.0F, radius};
    else corners = mark.point.y <= mark.baseline.y ? CornerRadii{radius, radius, 0.0F, 0.0F} :
                                                   CornerRadii{0.0F, 0.0F, radius, radius};
  }
  return Path::RoundedRect(mark.bounds, corners);
}

enum class Highlight { Hover, Selection, Cursor };

// Use compact representative geometry without reducing resolved brushes to a single palette color.
void PaintLegend(PaintContext& paint, Size size, Kind kind, const MarkStyle& style) {
  const Rect bounds{2.0F, 2.0F, std::max(0.0F, size.width - 4.0F), std::max(0.0F, size.height - 4.0F)};
  const Point center{size.width / 2.0F, size.height / 2.0F};
  paint.PushClip({0.0F, 0.0F, size.width, size.height});
  if (kind == Kind::Line) {
    Path line;
    line.MoveTo({bounds.x, center.y}).LineTo({bounds.x + bounds.width, center.y});
    paint.StrokePath(line, style.stroke, bounds, style.stroke_style);
  } else if (kind == Kind::Area || kind == Kind::Bar) {
    const auto path = Path::RoundedRect(bounds, CornerRadii{kind == Kind::Bar ? style.corner_radius : 0.0F});
    paint.FillPath(path, style.fill, bounds);
    if (kind == Kind::Area) {
      Path line;
      line.MoveTo({bounds.x, bounds.y}).LineTo({bounds.x + bounds.width, bounds.y});
      paint.StrokePath(line, style.stroke, bounds, style.stroke_style);
    } else {
      paint.StrokePath(path, style.stroke, bounds, style.stroke_style);
    }
  }
  if (kind != Kind::Bar && style.marker.visible.value_or(false) && style.marker.area.value_or(0.0F) > 0.0F) {
    const auto path = MarkerPath(center, 48.0F, *style.marker.shape);
    paint.FillPath(path, *style.marker.fill);
    paint.StrokePath(path, *style.marker.stroke, *style.marker.stroke_style);
  }
  paint.PopClip();
}

class ChartSurface::Extension final : public NodeExtension {
public:
  Extension(MountedNode&, const ChartSurface& spec) : spec_(spec), model_(BuildModel(*spec.presentation->config)) {}
  ~Extension() override { DismissTooltip(); }

  void Update(MountedNode&, const ChartSurface& spec) {
    const auto& previous = *spec_.presentation->config;
    const auto& next = *spec.presentation->config;
    const bool changed_model = !SameModel(previous, next);
    if (changed_model && next.animation.enabled && !spec.presentation->reduced_motion) {
      // Retain the old data with its geometry until stable keys have matched old and new mark indices.
      animation_from_ = geometry_;
      animation_data_ = previous.data;
    }
    if (changed_model) model_ = BuildModel(next);
    // Keep model rebuilding separate from layout and paint invalidation: a visual-only update need not
    // recompute domains or stacks, while a selection-only update can refresh pins without measuring axes.
    const bool changed_geometry = changed_model || previous.interpolation != next.interpolation ||
        previous.orientation != next.orientation || previous.sampling != next.sampling ||
        previous.scatter_sampling != next.scatter_sampling || previous.size_scale != next.size_scale ||
        !SameMarkerGeometry(spec_.presentation->markers, spec.presentation->markers);
    dirty_layout_ = dirty_layout_ || changed_geometry || !Access::Equal(previous.x, next.x) ||
        !Access::Equal(previous.y, next.y) || spec_.presentation->style.axis != spec.presentation->style.axis ||
        spec_.presentation->categories != spec.presentation->categories ||
        spec_.presentation->shaping != spec.presentation->shaping;
    animate_next_ = animate_next_ || (changed_model && next.animation.enabled && !spec.presentation->reduced_motion);
    spec_ = spec;
    if (!dirty_layout_) { RefreshPins(); RebuildSemantics(); }
    tooltip_dirty_ = true;
    InvalidatePaint(PaintInvalidation::Both);
    InvalidateSemantics();
  }

  PaintInvalidation PrepareGeometry(MountedNode& node, TextMeasurer& measurer) override {
    // Use final node-local content bounds for painting, hit testing and popup anchors. The supplied measurer
    // is borrowed only for this pass; cached tick metrics let unchanged frames avoid measuring again.
    enabled_ = node.IsEnabled();
    const auto bounds = node.ContentBounds();
    if (bounds != bounds_) { bounds_ = bounds; dirty_layout_ = true; }
    if (!dirty_layout_) {
      if (!animation_dirty_) return PaintInvalidation::None;
      if (animation_from_ && animation_target_) {
        geometry_ = InterpolateGeometry(*animation_from_, *animation_target_, motion_.Value(), C().interpolation);
        RebuildSemantics();
      }
      animation_dirty_ = false;
      tooltip_dirty_ = true;
      if (!motion_.IsRunning()) { animation_from_.reset(); animation_target_.reset(); animation_data_.reset(); }
      return PaintInvalidation::Both;
    }
    const auto& p = P();
    const auto& c = C();
    const bool horizontal = c.kind == Kind::Bar && c.orientation == ChartOrientation::Horizontal;
    const auto& horizontal_axis = horizontal ? c.y : c.x;
    const auto& vertical_axis = horizontal ? c.x : c.y;
    x_ticks_ = Ticks(horizontal_axis, horizontal ? model_.y : model_.x, bounds.width, p, !horizontal, measurer);
    y_ticks_ = Ticks(vertical_axis, horizontal ? model_.x : model_.y, bounds.height, p, horizontal, measurer);
    float label_width = 0.0F;
    float label_height = 0.0F;
    for (const auto& t : y_ticks_) label_width = std::max(label_width, t.metrics.advance);
    for (const auto& t : x_ticks_) label_height = std::max(label_height, t.metrics.font_metrics.LineHeight());
    const bool vertical_labels = Access::Visible(vertical_axis) && Access::Labels(vertical_axis);
    const bool horizontal_labels = Access::Visible(horizontal_axis) && Access::Labels(horizontal_axis);
    // Cap label gutters relative to the available size so long labels cannot consume the entire plot.
    float left = vertical_labels ? std::min(label_width + 10.0F, bounds.width * 0.3F) : 4.0F;
    float bottom = horizontal_labels ? std::min(label_height + 12.0F, bounds.height * 0.25F) : 4.0F;
    const auto& xtitle = horizontal ? p.y_title : p.x_title;
    const auto& ytitle = horizontal ? p.x_title : p.y_title;
    if (!xtitle.empty() && Access::Visible(horizontal_axis)) bottom += std::min(20.0F, bounds.height * 0.1F);
    const float top = !ytitle.empty() && Access::Visible(vertical_axis) ? std::min(24.0F, bounds.height * 0.15F) : 6.0F;
    Rect plot{bounds.x + left, bounds.y + top, std::max(0.0F, bounds.width - left - 8.0F),
              std::max(0.0F, bounds.height - top - bottom)};
    std::vector<ChartDatumIdentity> pins;
    if (c.selection) pins.push_back(*c.selection);
    if (cursor_) pins.push_back(*cursor_);
    geometry_ = BuildGeometry(c, model_, plot, p.markers, pins);
    base_mark_count_ = geometry_.marks.size();
    float last_right = -std::numeric_limits<float>::infinity();
    for (auto& t : x_ticks_) {
      t.position = plot.x + static_cast<float>(Fraction(t.value, horizontal ? model_.y : model_.x)) * plot.width;
      t.show_label = horizontal_labels && t.metrics.advance <= bounds.width && t.position - t.metrics.advance / 2 >= last_right;
      if (t.show_label) last_right = t.position + t.metrics.advance / 2 + 8.0F;
    }
    float last_bottom = -std::numeric_limits<float>::infinity();
    for (auto& t : y_ticks_) t.position = horizontal ? plot.y + static_cast<float>(Fraction(t.value, model_.x)) * plot.height :
        plot.y + (1.0F - static_cast<float>(Fraction(t.value, model_.y))) * plot.height;
    std::sort(y_ticks_.begin(), y_ticks_.end(), [](const auto& a, const auto& b) { return a.position < b.position; });
    for (auto& t : y_ticks_) {
      const float height = t.metrics.font_metrics.LineHeight();
      t.show_label = vertical_labels && t.metrics.advance + 8.0F <= left && t.position - height / 2 >= last_bottom;
      if (t.show_label) last_bottom = t.position + height / 2 + 4.0F;
    }
    dirty_layout_ = false;
    if (cursor_ && !Find(*cursor_)) cursor_.reset();
    if (hover_ && !Find(*hover_)) { hover_.reset(); hover_notification_pending_ = true; }
    tooltip_dirty_ = true;
    RebuildSemantics();
    if (animate_next_) {
      MatchAnimation();
      motion_.Set(0.0F);
      motion_.AnimateTo(1.0F, c.animation.update);
      if (animation_from_) {
        animation_target_ = geometry_;
        geometry_ = InterpolateGeometry(*animation_from_, *animation_target_, 0.0F, c.interpolation);
      }
      animate_next_ = false;
      RebuildSemantics();
    } else if (animation_target_) {
      animation_from_.reset();
      animation_target_.reset();
      animation_data_.reset();
      motion_.Set(1.0F);
    }
    return PaintInvalidation::Both;
  }

  FrameResult OnFrame(MountedNode& node, const FrameInfo& frame) override {
    enabled_ = node.IsEnabled();
    if (!enabled_) { hover_.reset(); cursor_.reset(); keyboard_cursor_ = false; pressed_.reset(); DismissTooltip(); }
    // Geometry preparation only records invalidated hover state; notify the application from the frame callback.
    if (hover_notification_pending_) {
      hover_notification_pending_ = false;
      EmitEvent<ChartEvents::HoverChanged>(std::optional<ChartDatumInfo>{});
    }
    if (tooltip_dirty_ && !dirty_layout_) { UpdateTooltip(); tooltip_dirty_ = false; }
    auto policy = frame;
    policy.reduced_motion = policy.reduced_motion || P().reduced_motion;
    // Frame progression changes retained geometry/paint, not application State or the composition tree.
    const auto advance = motion_.Advance(policy);
    if (advance.changed) { animation_dirty_ = true; InvalidatePaint(PaintInvalidation::Both); }
    return {.needs_frame = advance.needs_frame || dirty_layout_ || tooltip_dirty_, .wake_after = advance.wake_after};
  }

  bool HitTest(MountedNode&, Point point) const override { return geometry_.plot.Contains(point); }
  bool HoverHitTest(MountedNode&, Point point) const override { return geometry_.plot.Contains(point); }

  void OnHover(MountedNode&, const HoverEvent& event) override {
    const auto hit = event.type == HoverEventType::Leave ? std::nullopt : Hit(geometry_, event.position, Slop());
    std::optional<ChartDatumIdentity> next;
    if (hit) next = Info(*hit).identity;
    // Moving within one datum is not an identity transition and must not repeatedly emit HoverChanged.
    if (next == hover_) return;
    hover_ = next;
    EmitEvent<ChartEvents::HoverChanged>(hit ? std::optional(Info(*hit)) : std::nullopt);
    UpdateTooltip();
    RebuildSemantics();
    InvalidatePaint();
  }

  PointerResult OnPointer(MountedNode& node, const PointerEvent& event) override {
    if (!node.IsEnabled()) { pressed_.reset(); return PointerResult::Ignored; }
    if (event.type == PointerEventType::Cancel) {
      pressed_.reset();
      DismissTooltip();
      return PointerResult::Ignored;
    }
    if (event.type == PointerEventType::Down) {
      // A pointer-acquired focus is not keyboard navigation, even though both retain a tooltip cursor.
      if (keyboard_cursor_) { keyboard_cursor_ = false; InvalidatePaint(); }
      // Observe a primary-only sequence without committing selection; dragging or cancellation can still win.
      if (event.changed_button != PointerButton::Primary || event.pressed_buttons != PointerButton::Primary) {
        pressed_.reset();
        return PointerResult::Ignored;
      }
      auto hit = Hit(geometry_, event.position, Slop());
      if (!hit) return PointerResult::Ignored;
      pressed_ = Info(*hit).identity;
      pointer_id_ = event.pointer_id;
      down_ = event.position;
      return PointerResult::Observe;
    }
    if (!pressed_ || pointer_id_ != event.pointer_id) return PointerResult::Ignored;
    if (event.type == PointerEventType::Move) {
      if (event.pressed_buttons != PointerButton::Primary || std::hypot(event.position.x - down_.x, event.position.y - down_.y) > 8.0F) {
        pressed_.reset();
        return PointerResult::Ignored;
      }
      return PointerResult::Observe;
    }
    if (event.type == PointerEventType::Up) {
      // Re-hit the current geometry and compare stable identities because data may change between Down and Up.
      const auto identity = *pressed_;
      pressed_.reset();
      const auto hit = Hit(geometry_, event.position, Slop());
      if (event.changed_button == PointerButton::Primary && event.pressed_buttons == PointerButton::None &&
          hit && Info(*hit).identity == identity) {
        Activate(*hit);
        return PointerResult::Handled;
      }
    }
    return PointerResult::Ignored;
  }

  void OnFocusChanged(MountedNode&, bool focused) override {
    focused_ = focused;
    if (!focused) { cursor_.reset(); keyboard_cursor_ = false; UpdateTooltip(); }
    RebuildSemantics();
    InvalidatePaint();
  }

  bool OnKey(MountedNode& node, const KeyEvent& event) override {
    if (!node.IsEnabled() || event.type != KeyEventType::Down || event.modifiers.control || event.modifiers.alt || event.modifiers.meta) return false;
    if (event.key == Key::Escape) {
      const bool active = cursor_.has_value() || tooltip_id_.has_value();
      cursor_.reset();
      keyboard_cursor_ = false;
      DismissTooltip();
      InvalidatePaint();
      return active;
    }
    const auto current = cursor_ ? Find(*cursor_) : (C().selection ? Find(*C().selection) : std::nullopt);
    if (event.key == Key::Enter || event.key == Key::Space) {
      if (!current) return false;
      keyboard_cursor_ = true;
      Activate(*current);
      return true;
    }
    if (event.key != Key::ArrowLeft && event.key != Key::ArrowRight && event.key != Key::ArrowUp &&
        event.key != Key::ArrowDown && event.key != Key::Home && event.key != Key::End) return false;
    const auto next = Navigate(current, event.key);
    if (!next) return false;
    keyboard_cursor_ = true;
    cursor_ = DatumInfo(*C().data, *next).identity;
    RefreshPins();
    UpdateTooltip();
    RebuildSemantics();
    InvalidatePaint();
    return true;
  }

  void BuildSemantics(SemanticBuilder& builder) const override {
    std::string summary = P().summary;
    if (summary.empty()) summary = model_.count == 0 ? P().empty_label : Number(static_cast<double>(model_.count)) + " " + P().count_label;
    if (C().scatter_sampling == ScatterSampling::Representative) summary += "; " + P().representative_label;
    builder.SetOwner(Semantics{.role = SemanticRole::Generic, .label = summary,
                               .collection = SemanticCollection{.item_count = model_.count}});
    // Expose bounded virtual data children rather than allocating a View for every sample.
    // Their values and collection indices refer to original input, even when geometry is sampled or stacked.
    for (const auto& [id, index] : semantic_marks_) {
      const auto info = Info(index);
      const auto context = TooltipContext(info);
      const auto& mark = geometry_.marks[index];
      Rect bounds = mark.bounds;
      if (bounds.width < 12.0F) { bounds.x -= (12.0F - bounds.width) / 2; bounds.width = 12.0F; }
      if (bounds.height < 12.0F) { bounds.y -= (12.0F - bounds.height) / 2; bounds.height = 12.0F; }
      builder.AddChild(id, bounds.Intersection(geometry_.plot),
                       Semantics{.role = SemanticRole::Generic, .label = context.series_label + ", " + context.position_text,
                                 .value = context.value_text, .selected = C().selection == info.identity,
                                 .collection_item = SemanticCollectionItem{.index = info.datum_index, .row_index = info.series_index}}, enabled_);
      if (enabled_) {
        builder.AddAction(id, SemanticActionKind::Activate);
        builder.AddAction(id, SemanticActionKind::Focus);
      }
    }
  }

  bool OnSemanticAction(std::uint64_t id, const SemanticAction& action) override {
    if (!enabled_) return false;
    auto found = std::find_if(semantic_marks_.begin(), semantic_marks_.end(), [&](const auto& item) { return item.first == id; });
    if (found == semantic_marks_.end()) return false;
    if (action.kind == SemanticActionKind::Activate) {
      keyboard_cursor_ = true;
      Activate(found->second);
      return true;
    }
    if (action.kind == SemanticActionKind::Focus) {
      keyboard_cursor_ = true;
      cursor_ = Info(found->second).identity;
      UpdateTooltip();
      InvalidatePaint();
      return true;
    }
    return false;
  }

  void PaintBehindContent(const MountedNode&, PaintContext& paint) const override {
    const auto& p = P();
    const auto& c = C();
    const auto plot = geometry_.plot;
    if (bounds_.IsEmpty()) return;
    paint.PushClip(bounds_);
    paint.DrawRect(plot, *p.style.background);
    const bool horizontal = c.kind == Kind::Bar && c.orientation == ChartOrientation::Horizontal;
    const auto& xa = horizontal ? c.y : c.x;
    const auto& ya = horizontal ? c.x : c.y;
    const StrokeStyle stroke{.width = *p.style.axis.width};
    for (const auto& t : x_ticks_) {
      if (Access::Grid(xa)) paint.DrawLine({t.position, plot.y}, {t.position, plot.y + plot.height}, *p.style.axis.grid, stroke);
      paint.DrawLine({t.position, plot.y + plot.height}, {t.position, plot.y + plot.height + 4.0F}, *p.style.axis.line, stroke);
      if (t.show_label) DrawLabel(paint, t, {std::clamp(t.position - t.metrics.advance / 2, bounds_.x,
          std::max(bounds_.x, bounds_.x + bounds_.width - t.metrics.advance)), plot.y + plot.height + 7.0F});
    }
    for (const auto& t : y_ticks_) {
      if (Access::Grid(ya)) paint.DrawLine({plot.x, t.position}, {plot.x + plot.width, t.position}, *p.style.axis.grid, stroke);
      paint.DrawLine({plot.x - 4.0F, t.position}, {plot.x, t.position}, *p.style.axis.line, stroke);
      if (t.show_label) DrawLabel(paint, t, {plot.x - 8.0F - t.metrics.advance, t.position - t.metrics.font_metrics.LineHeight() / 2});
    }
    if (Access::Visible(xa)) paint.DrawLine({plot.x, plot.y + plot.height}, {plot.x + plot.width, plot.y + plot.height}, *p.style.axis.line, stroke);
    if (Access::Visible(ya)) paint.DrawLine({plot.x, plot.y}, {plot.x, plot.y + plot.height}, *p.style.axis.line, stroke);
    if (Access::Visible(xa)) paint.DrawText({plot.x, bounds_.y + bounds_.height - 19.0F, plot.width, 19.0F},
        horizontal ? p.y_title : p.x_title, *p.style.axis.text, {.shaping = p.shaping, .align = TextAlign::Center, .wrap = TextWrap::NoWrap});
    if (Access::Visible(ya)) paint.DrawText({plot.x, bounds_.y, plot.width, 20.0F}, horizontal ? p.x_title : p.y_title,
        *p.style.axis.text, {.shaping = p.shaping, .wrap = TextWrap::NoWrap});
    paint.PushClip(plot);
    PaintMarks(paint);
    if (model_.count == 0) paint.DrawText(plot, p.empty_label, *p.style.axis.text,
        {.shaping = p.shaping, .align = TextAlign::Center, .vertical_align = TextVerticalAlign::Center});
    paint.PopClip();
    paint.PopClip();
  }

  void PaintAboveContent(const MountedNode&, PaintContext& paint) const override {
    // Transient highlights live in the foreground so hover can repaint without rerecording axes and data marks.
    paint.PushClip(geometry_.plot);
    const auto keyboard = focused_ && keyboard_cursor_ ? cursor_ : std::nullopt;
    // Resolve overlapping states by identity before painting; alpha must not accumulate on the same datum.
    if (hover_ && hover_ != C().selection && hover_ != keyboard) PaintHighlight(paint, *hover_, Highlight::Hover);
    if (C().selection && C().selection != keyboard) PaintHighlight(paint, *C().selection, Highlight::Selection);
    if (keyboard) PaintHighlight(paint, *keyboard, Highlight::Cursor);
    paint.PopClip();
  }

private:
  const Presentation& P() const { return *spec_.presentation; }
  const Config& C() const { return *P().config; }
  float Slop() const { return *P().style.interaction.hit_slop; }
  ChartDatumInfo Info(std::size_t i) const { return DatumInfo(*C().data, geometry_.marks[i]); }

  void PaintHighlight(PaintContext& paint, const ChartDatumIdentity& identity, Highlight state) const {
    const auto found = Find(identity);
    const float width = *P().style.interaction.width;
    auto color = *P().style.interaction.highlight;
    if (!found || width == 0.0F || color.alpha == 0.0F) return;
    if (state == Highlight::Hover) color.alpha *= 0.45F;
    const auto& mark = geometry_.marks[*found];
    const auto& style = P().marks[mark.series];
    Path outline;
    if (C().kind == Kind::Bar) {
      outline = BarPath(mark, C().orientation, style.corner_radius);
    } else {
      const bool visible = style.marker.visible.value_or(false) && mark.area > 0.0F;
      const float area = visible ? mark.area : 36.0F;
      const auto shape = visible ? mark.shape : ChartMarkerShape::Circle;
      // Unit-area geometry avoids coordinate cancellation and underflow for extremely small markers.
      const auto bounds = MarkerBounds({}, 1.0F, shape);
      const float stroke_width = visible ? style.marker.stroke_style->width : 2.0F;
      const double gap = 2.0 + (stroke_width + static_cast<double>(width)) / 2.0;
      const double extent = std::sqrt(static_cast<double>(area)) + 2.0 * gap / std::min(bounds.width, bounds.height);
      const auto ring_area = static_cast<float>(std::min(extent * extent,
                                                       static_cast<double>(std::numeric_limits<float>::max())));
      outline = MarkerPath(mark.point, ring_area, shape);
      // Lines and areas without visible markers still need a precise, series-colored datum anchor.
      if (!visible) {
        const auto anchor = MarkerPath(mark.point, area, shape);
        paint.FillPath(anchor, P().surface);
        paint.StrokePath(anchor, *style.marker.stroke, StrokeStyle{.width = 2.0F});
      }
    }
    const StrokeStyle stroke{.width = width, .cap = StrokeCap::Round, .join = StrokeJoin::Round};
    if (state == Highlight::Cursor) {
      // A theme-surface underlay separates keyboard focus from both the mark and the plotting background.
      paint.StrokePath(outline, P().surface, StrokeStyle{.width = width + 2.0F, .join = StrokeJoin::Round});
    }
    paint.StrokePath(outline, color, stroke);
  }

  void RefreshPins() {
    // Extra interaction pins form a disposable suffix; contour indices continue to reference the base geometry.
    // Replacing that suffix prevents cursor movement from accumulating marks removed by sampling.
    if (geometry_.marks.size() > base_mark_count_) geometry_.marks.resize(base_mark_count_);
    std::vector<ChartDatumIdentity> pins;
    if (C().selection) pins.push_back(*C().selection);
    if (cursor_) pins.push_back(*cursor_);
    for (const auto& identity : pins) {
      if (Find(identity)) continue;
      for (std::size_t s = 0; s < C().data->entries.size(); ++s) {
        const auto& entry = C().data->entries[s];
        if (!entry.visible || entry.key != identity.series_key) continue;
        auto found = entry.index->by_key.find(identity.datum_key);
        if (found != entry.index->by_key.end()) if (auto mark = Project(s, found->second)) geometry_.marks.push_back(*mark);
      }
    }
    RefreshHitIndex(geometry_);
  }

  void MatchAnimation() {
    if (!animation_from_ || !animation_data_ || animation_from_->marks.size() != geometry_.marks.size() ||
        animation_from_->contours.size() != geometry_.contours.size()) {
      animation_from_.reset();
      return;
    }
    const auto clipped = [](const Geometry& geometry) {
      return !geometry.contours.empty() && std::any_of(geometry.marks.begin(), geometry.marks.end(), [&](const Mark& mark) {
        return !geometry.plot.Contains(mark.point) ||
               (geometry.kind == Kind::Area && !geometry.plot.Contains(mark.baseline));
      });
    };
    // Clipping can change path topology; fade instead of morphing saturated offscreen anchors.
    if (clipped(*animation_from_) || clipped(geometry_)) {
      animation_from_.reset();
      return;
    }
    std::unordered_map<std::string, std::size_t> previous;
    for (std::size_t i = 0; i < animation_from_->marks.size(); ++i) {
      previous.emplace(IdentityKey(DatumInfo(*animation_data_, animation_from_->marks[i]).identity), i);
    }
    // Align old positions with target mark order by identity, not by transient geometry-vector position.
    Geometry matched = geometry_;
    std::vector<std::size_t> mapping;
    for (std::size_t i = 0; i < geometry_.marks.size(); ++i) {
      auto found = previous.find(IdentityKey(Info(i).identity));
      if (found == previous.end()) { animation_from_.reset(); return; }
      matched.marks[i] = animation_from_->marks[found->second];
      mapping.push_back(found->second);
    }
    // Matching keys alone is insufficient: changed contour neighbors could morph across a new gap or reorder.
    std::unordered_map<std::size_t, std::size_t> previous_neighbors;
    for (const auto& contour : animation_from_->contours) {
      for (std::size_t i = 1; i < contour.marks.size(); ++i) previous_neighbors[contour.marks[i]] = contour.marks[i - 1];
    }
    for (const auto& contour : geometry_.contours) {
      for (std::size_t i = 1; i < contour.marks.size(); ++i) {
        const auto found = previous_neighbors.find(mapping[contour.marks[i]]);
        if (found == previous_neighbors.end() || found->second != mapping[contour.marks[i - 1]]) {
          animation_from_.reset();
          return;
        }
      }
    }
    animation_from_ = std::move(matched);
  }

  std::optional<std::size_t> Find(const ChartDatumIdentity& identity) const {
    for (std::size_t i = 0; i < geometry_.marks.size(); ++i) {
      const auto& mark = geometry_.marks[i];
      if (C().data->entries[mark.series].key != identity.series_key) continue;
      auto sample = C().data->entries[mark.series].index->by_key.find(identity.datum_key);
      if (sample != C().data->entries[mark.series].index->by_key.end() && sample->second == mark.sample &&
          (geometry_.plot.Contains(mark.point) || (C().kind == Kind::Bar && mark.bounds.Intersects(geometry_.plot)))) return i;
    }
    return {};
  }

  void Activate(std::size_t index) {
    // Only the transient cursor is owned here. The application decides whether to accept the selection request.
    const auto info = Info(index);
    cursor_ = info.identity;
    UpdateTooltip();
    RebuildSemantics();
    InvalidatePaint();
    EmitEvent<ChartEvents::SelectionRequested>(info);
  }

  std::optional<Mark> Project(std::size_t series, std::size_t sample) const {
    const auto& p = model_.series[series][sample];
    if (p.gap || p.x < model_.x.min || p.x > model_.x.max) return {};
    if (C().kind == Kind::Bar) {
      if (std::max(p.lower, p.upper) < model_.y.min || std::min(p.lower, p.upper) > model_.y.max) return {};
    } else if (p.upper < model_.y.min || p.upper > model_.y.max) return {};
    // Project only the requested mark, preserving group slots without rebuilding paths or the hit index.
    std::size_t slot = 0;
    std::size_t visible_count = 0;
    if (C().kind == Kind::Bar && C().bar_mode == BarMode::Grouped) {
      for (std::size_t s = 0; s < C().data->entries.size(); ++s) {
        if (!C().data->entries[s].visible) continue;
        ++visible_count;
        if (s < series) ++slot;
      }
    }
    return ProjectMark(C(), model_, geometry_.plot, series, p, P().markers[series], slot, visible_count);
  }

  // Navigate the unsampled model so display reduction never makes intermediate source data unreachable.
  // Only eligible visible candidates are projected back into retained geometry as interaction pins.
  std::optional<Mark> Navigate(std::optional<std::size_t> current, Key key) const {
    if (key == Key::Home || key == Key::End || !current) {
      const bool reverse = key == Key::End;
      for (std::size_t n = 0; n < model_.series.size(); ++n) {
        const auto s = reverse ? model_.series.size() - n - 1 : n;
        const auto& samples = model_.series[s];
        for (std::size_t j = 0; j < samples.size(); ++j) {
          const auto i = reverse ? samples.size() - j - 1 : j;
          if (auto mark = Project(s, i)) return mark;
        }
      }
      return {};
    }
    const auto& from = geometry_.marks[*current];
    const auto& raw = model_.series[from.series][from.sample];
    const bool horizontal = C().kind == Kind::Bar && C().orientation == ChartOrientation::Horizontal;
    const bool along = horizontal ? (key == Key::ArrowUp || key == Key::ArrowDown) : (key == Key::ArrowLeft || key == Key::ArrowRight);
    if (C().kind != Kind::Scatter && along) {
      const bool forward = horizontal ? key == Key::ArrowDown : key == Key::ArrowRight;
      auto i = static_cast<std::ptrdiff_t>(from.sample);
      const auto count = static_cast<std::ptrdiff_t>(model_.series[from.series].size());
      while ((i += forward ? 1 : -1) >= 0 && i < count) {
        if (auto mark = Project(from.series, static_cast<std::size_t>(i))) return mark;
      }
      return {};
    }
    if (C().kind != Kind::Scatter) {
      const bool forward = horizontal ? key == Key::ArrowRight : key == Key::ArrowDown;
      auto s = static_cast<std::ptrdiff_t>(from.series);
      while ((s += forward ? 1 : -1) >= 0 && s < static_cast<std::ptrdiff_t>(model_.series.size())) {
        const auto& samples = model_.series[static_cast<std::size_t>(s)];
        auto right = std::lower_bound(samples.begin(), samples.end(), raw.x, [](const auto& p, double x) { return p.x < x; });
        auto left = right;
        while (left != samples.begin() || right != samples.end()) {
          const bool use_left = left != samples.begin() && (right == samples.end() || raw.x - (left - 1)->x <= right->x - raw.x);
          auto candidate = use_left ? --left : right++;
          if (auto mark = Project(static_cast<std::size_t>(s), static_cast<std::size_t>(candidate - samples.begin()))) return mark;
        }
      }
      return {};
    }
    std::optional<Mark> nearest;
    double best = std::numeric_limits<double>::infinity();
    const double fx = Fraction(raw.x, model_.x) * geometry_.plot.width;
    const double fy = Fraction(raw.upper, model_.y) * geometry_.plot.height;
    for (std::size_t s = 0; s < model_.series.size(); ++s) {
      for (std::size_t i = 0; i < model_.series[s].size(); ++i) {
        const auto& sample = model_.series[s][i];
        if (sample.gap || sample.x < model_.x.min || sample.x > model_.x.max || sample.upper < model_.y.min || sample.upper > model_.y.max) continue;
        const double dx = Fraction(sample.x, model_.x) * geometry_.plot.width - fx;
        const double dy = fy - Fraction(sample.upper, model_.y) * geometry_.plot.height;
        const double direction = key == Key::ArrowRight ? dx : key == Key::ArrowLeft ? -dx : key == Key::ArrowDown ? dy : -dy;
        if (direction <= 0) continue;
        const double distance = std::hypot(dx, dy);
        if (distance < best) {
          // A zero-area bubble cannot receive focus; keep searching beyond invisible candidates.
          if (auto mark = Project(s, i)) { best = distance; nearest = mark; }
        }
      }
    }
    return nearest;
  }

  ChartTooltipContext TooltipContext(const ChartDatumInfo& info) const {
    ChartTooltipContext context{.datum = info, .series_label = P().series_labels[info.series_index]};
    std::visit([&](const auto& value) {
      if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ChartCategoryValue>) {
        const auto& sample = C().data->entries[info.series_index].index->samples[
            C().data->entries[info.series_index].index->by_key.at(info.identity.datum_key)];
        context.position_text = P().categories[static_cast<std::size_t>(sample.x)];
        context.value_text = Format(C().y, value.value);
      } else {
        context.position_text = Format(C().x, value.x);
        context.value_text = Format(C().y, value.y);
        if constexpr (requires { value.magnitude; }) if (value.magnitude) context.value_text += " · " + Number(*value.magnitude);
      }
    }, info.value);
    if (info.normalized_value) context.value_text += " (" + Number(*info.normalized_value * 100.0) + "%)";
    return context;
  }

  void DismissTooltip() {
    if (tooltip_id_) spec_.popup.Dismiss(*tooltip_id_);
    tooltip_id_.reset();
    tooltip_identity_.reset();
  }

  void UpdateTooltip() {
    const auto identity = hover_ ? hover_ : cursor_;
    const auto mark = identity ? Find(*identity) : std::nullopt;
    if (!enabled_ || !C().tooltip->enabled || !mark) { DismissTooltip(); return; }
    auto context = TooltipContext(Info(*mark));
    auto custom = C().tooltip->content;
    const auto style = P().tooltip;
    // Popup content may be built after this callback returns; capture owned values rather than geometry references.
    ViewFactory factory = [context, custom, style] {
      if (custom) return custom(context);
      return Text(context.series_label + "\n" + context.position_text + ": " + context.value_text)
          .Style(style.text_style)
          .With(Padding(style.padding), Background(style.background), CornerRadius(style.corner_radius),
                style.shadow, Frame{.max_width = style.maximum_width, .min_height = style.minimum_height});
    };
    const auto anchor = geometry_.marks[*mark].bounds;
    // Move the existing anchored popup and replace content only when needed, instead of dismissing on every move.
    if (tooltip_id_) {
      if (!spec_.popup.UpdateAnchor(*tooltip_id_, anchor)) tooltip_id_.reset();
      else if (tooltip_dirty_ || tooltip_identity_ != identity) spec_.popup.Update(*tooltip_id_, factory);
    }
    if (!tooltip_id_) tooltip_id_ = spec_.popup.ShowAtAnchor(anchor, factory,
        PopupOptions{.placement = style.placement, .gap = style.gap, .viewport_margin = style.viewport_margin,
                     .dismiss_on_outside_press = false, .dismiss_on_cancel = false, .retain_anchor_focus = true});
    tooltip_identity_ = identity;
  }

  void RebuildSemantics() {
    semantic_marks_.clear();
    std::unordered_set<std::size_t> selected;
    std::unordered_set<std::uint64_t> ids;
    auto add = [&](std::size_t index) {
      if (semantic_marks_.size() >= C().accessibility.max_data_nodes || !selected.insert(index).second) return;
      const auto& mark = geometry_.marks[index];
      if (!geometry_.plot.Contains(mark.point) && !(C().kind == Kind::Bar && mark.bounds.Intersects(geometry_.plot))) return;
      const auto identity = Info(index).identity;
      auto id = StableHash(IdentityKey(identity));
      // Reserve zero and resolve the unlikely hash collision within this chart's exposed semantic children.
      if (id == 0) id = 1;
      while (!ids.insert(id).second) ++id;
      semantic_marks_.emplace_back(id, index);
    };
    // Allocate the bounded semantic budget to selection and cursor first, then sample the remaining marks.
    if (C().selection) if (auto index = Find(*C().selection)) add(*index);
    if (cursor_) if (auto index = Find(*cursor_)) add(*index);
    const auto count = geometry_.marks.size();
    const auto stride = std::max<std::size_t>(1, count / C().accessibility.max_data_nodes);
    for (std::size_t i = 0; i < count; i += stride) add(i);
    InvalidateSemantics();
  }

  void DrawLabel(PaintContext& paint, const TickLayout& tick, Point origin) const {
    paint.DrawTextRun({origin.x, origin.y, tick.metrics.advance, tick.metrics.font_metrics.LineHeight()},
                      {origin.x, origin.y + tick.metrics.font_metrics.ascent}, tick.text, *P().style.axis.text, P().shaping);
  }

  void PaintMarks(PaintContext& paint) const {
    const auto& c = C();
    // Matched updates morph at full opacity; unmatched updates reuse motion progress for a fade-in.
    const float opacity = animation_target_ ? 1.0F : motion_.Value();
    // Explicit plot-wide brush bounds keep gradients continuous across separately clipped contours and bars.
    for (const auto& contour : geometry_.contours) {
      const auto& style = P().marks[contour.series];
      if (c.kind == Kind::Area) paint.FillPath(contour.fill, WithOpacity(style.fill, opacity), geometry_.plot);
      paint.StrokePath(contour.line, WithOpacity(style.stroke, opacity), geometry_.plot, style.stroke_style);
    }
    for (const auto& mark : geometry_.marks) {
      const auto& style = P().marks[mark.series];
      if (c.kind == Kind::Bar) {
        const auto path = BarPath(mark, c.orientation, style.corner_radius);
        paint.FillPath(path, WithOpacity(style.fill, opacity), geometry_.plot);
        paint.StrokePath(path, WithOpacity(style.stroke, opacity), geometry_.plot, style.stroke_style);
      } else if (style.marker.visible.value_or(false) && mark.area > 0.0F) {
        const auto path = MarkerPath(mark.point, mark.area, mark.shape);
        paint.FillPath(path, WithOpacity(*style.marker.fill, opacity));
        paint.StrokePath(path, WithOpacity(*style.marker.stroke, opacity), *style.marker.stroke_style);
      }
    }
  }

  ChartSurface spec_;
  Model model_;
  Geometry geometry_;
  std::size_t base_mark_count_ = 0;
  std::optional<Geometry> animation_from_;
  std::optional<Geometry> animation_target_;
  std::shared_ptr<const Data> animation_data_;
  Rect bounds_;
  std::vector<TickLayout> x_ticks_;
  std::vector<TickLayout> y_ticks_;
  std::vector<std::pair<std::uint64_t, std::size_t>> semantic_marks_;
  std::optional<ChartDatumIdentity> hover_;
  std::optional<ChartDatumIdentity> cursor_;
  std::optional<ChartDatumIdentity> pressed_;
  std::optional<ChartDatumIdentity> tooltip_identity_;
  std::optional<LayerId> tooltip_id_;
  std::int64_t pointer_id_ = 0;
  Point down_;
  MotionController motion_{1.0F};
  bool dirty_layout_ = true;
  bool tooltip_dirty_ = false;
  bool hover_notification_pending_ = false;
  bool animate_next_ = false;
  bool animation_dirty_ = false;
  bool focused_ = false;
  bool keyboard_cursor_ = false;
  bool enabled_ = true;
};

} // namespace

// The enclosing public Scope owns these hooks and the externally bound typed events.
[[huxerui::composable]] View BuildChart(std::shared_ptr<const Config> config) {
  ValidateConfig(*config);
  const auto presentation = UsePresentation(config);
  const auto events = UseEvents();
  const auto popup = UsePopup();
  // The extension emits on its mounted Canvas. Forward those bindings to the enclosing component emitter
  // so each chart's externally attached handlers receive only that chart's events.
  View surface = Canvas([](PaintContext&, Size) {})
      .With(ChartSurface{presentation, popup}, popup.Anchor(), Focusable(), Grow(),
            Frame{.min_width = 240.0F, .min_height = 180.0F})
      .On<ChartEvents::SelectionRequested>([events](const ChartDatumInfo& info) { events.Emit<ChartEvents::SelectionRequested>(info); })
      .On<ChartEvents::HoverChanged>([events](const std::optional<ChartDatumInfo>& info) { events.Emit<ChartEvents::HoverChanged>(info); });
  Views legend_items;
  if (config->legend.visible) {
    for (std::size_t i = 0; i < config->data->entries.size(); ++i) {
      const auto& entry = config->data->entries[i];
      View label = Row {
        Canvas([style = presentation->marks[i], kind = config->kind](PaintContext& paint, Size size) {
          PaintLegend(paint, size, kind, style);
        }).With(Frame{.width = 24.0F, .height = 16.0F}),
        Text(presentation->series_labels[i]).Style(*presentation->style.legend.text),
      }.With(Spacing(6.0F), CrossAlign(CrossAxisAlignment::Center), Padding(4.0F),
             Opacity(entry.visible ? 1.0F : 0.45F));
      if (config->legend.interactive) {
        // Visibility remains controlled by the input snapshot; the entry requests its next value without mutating it.
        legend_items.Add(std::move(label)
            .OnClick([events, key = entry.key, visible = entry.visible] {
              events.Emit<ChartEvents::SeriesVisibilityRequested>(ChartSeriesVisibilityRequest{key, !visible});
            })
            .With(Semantics{.role = SemanticRole::Checkbox, .label = presentation->series_labels[i],
                            .checked = entry.visible ? SemanticCheckedState::Checked : SemanticCheckedState::Unchecked,
                            .descendants = SemanticDescendantPolicy::Exclude})
            .Key(entry.key));
      } else {
        legend_items.Add(std::move(label).Key(entry.key));
      }
    }
  }
  View legend = Flow {legend_items}.With(Spacing(*presentation->style.legend.spacing));
  View content = surface;
  if (config->legend.visible) {
    switch (config->legend.placement) {
      case ChartLegendPlacement::Top: content = Column {legend, surface}.With(Grow()); break;
      case ChartLegendPlacement::Bottom: content = Column {surface, legend}.With(Grow()); break;
      case ChartLegendPlacement::Start: content = Row {legend, surface}.With(Grow()); break;
      case ChartLegendPlacement::End: content = Row {surface, legend}.With(Grow()); break;
    }
  }
  if (presentation->title.empty()) return content;
  return Column {
    Text(presentation->title, TextRole::Title).Style(*presentation->style.title),
    content,
  }.With(Spacing(8.0F), CrossAlign(CrossAxisAlignment::Stretch));
}

} // namespace huxerui::charts_detail
