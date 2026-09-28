#include "chart_internal.h"
#include <charts_resources.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_set>

namespace huxerui::charts_detail {
namespace {

struct ResolvedSliceStyle {
  Brush fill;
  Brush stroke;
  StrokeStyle stroke_style{.width = 1.0F, .join = StrokeJoin::Round};
};

void Merge(ResolvedSliceStyle& target, const PieChartStyle& source) {
  if (source.fill) target.fill = *source.fill;
  if (source.stroke) target.stroke = *source.stroke;
  if (source.stroke_style) target.stroke_style = *source.stroke_style;
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

Path SectorPath(Point center, float radius, double start, double sweep, bool whole_circle) {
  const auto steps = std::clamp(
      static_cast<int>(std::ceil(sweep * static_cast<double>(radius) / 3.0)), 2, 512);
  Path path;
  if (whole_circle) {
    const double angle = start;
    path.MoveTo({center.x + radius * static_cast<float>(std::cos(angle)),
                 center.y + radius * static_cast<float>(std::sin(angle))});
    for (int step = 1; step < steps; ++step) {
      const double current = start + sweep * static_cast<double>(step) / static_cast<double>(steps);
      path.LineTo({center.x + radius * static_cast<float>(std::cos(current)),
                   center.y + radius * static_cast<float>(std::sin(current))});
    }
  } else {
    path.MoveTo(center);
    for (int step = 0; step <= steps; ++step) {
      const double current = start + sweep * static_cast<double>(step) / static_cast<double>(steps);
      path.LineTo({center.x + radius * static_cast<float>(std::cos(current)),
                   center.y + radius * static_cast<float>(std::sin(current))});
    }
  }
  path.Close();
  return path;
}

void PaintPie(PaintContext& context, Size size, std::span<const double> shares,
              std::span<const ResolvedSliceStyle> styles, Color empty_color) {
  const float radius = std::max(0.0F, std::min(size.width, size.height) * 0.5F - 1.0F);
  if (radius <= 0.0F) return;

  const Point center{size.width * 0.5F, size.height * 0.5F};
  const std::size_t positive_count = static_cast<std::size_t>(std::count_if(
      shares.begin(), shares.end(), [](double share) { return share > 0.0; }));
  if (positive_count == 0) {
    context.DrawCircle(center, radius, empty_color);
    return;
  }

  double start = -std::numbers::pi_v<double> * 0.5;
  for (std::size_t index = 0; index < shares.size(); ++index) {
    const double sweep = shares[index] * 2.0 * std::numbers::pi_v<double>;
    if (sweep <= 0.0) continue;
    const auto path = SectorPath(center, radius, start, sweep, positive_count == 1);
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
    Validate(style.stroke_style.value_or(StrokeStyle{.width = 1.0F, .join = StrokeJoin::Round}));
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
  const auto plot = Canvas([shares, styles, empty_color, background = appearance.background](PaintContext& context, Size size) {
    if (background) context.FillPath(Path::RoundedRect({0.0F, 0.0F, size.width, size.height}, {}), *background);
    PaintPie(context, size, shares, styles, empty_color);
  }).With(Semantics{.role = SemanticRole::Image, .label = summary});
  if (title.empty()) return plot;
  return Column {
    Text(title).Style(*appearance.title),
    plot,
  }.With(Spacing(theme.spacing.small), CrossAlign(CrossAxisAlignment::Stretch));
}

} // namespace huxerui::charts_detail
