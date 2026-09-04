#include "chart_internal.h"

#include <huxerui/app.h>
#include <huxerui/platform_adapter.h>
#include <huxerui/render_scene.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace huxerui;
using namespace huxerui::charts_detail;

namespace {
int checks = 0;
void Check(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class Function> void Reject(Function function) {
  bool rejected = false;
  try { function(); } catch (const std::invalid_argument&) { rejected = true; }
  Check(rejected, "Expected invalid_argument");
}

CategoryChartData Categories() {
  return CategoryChartData({{"a", "A"}, {"b", "B"}, {"c", "C"}}, {
    CategorySeries("first", "First", {{"a1", "a", 10}, {"b1", "b", 20}, {"c1", "c", -5}}),
    CategorySeries("second", "Second", {{"a2", "a", 30}, {"b2", "b", 40}, {"c2", "c", -15}}),
  });
}

Config BarConfig(CategoryChartData data) {
  Config config;
  config.kind = Kind::Bar;
  config.data = Access::DataOf(data);
  config.x = ChartAxis::Category();
  config.y = ChartAxis::Linear().IncludeZero();
  config.style = BarChartStyle{};
  return config;
}

void DataTests() {
  Reject([] { XYChartData({XYSeries("s", "S", {{"a", 1, 2}, {"b", 1, 3}})}); });
  Reject([] { XYChartData({XYSeries("s", "S", {{"a", 0, 2}, {"a", 1, 3}})}); });
  Reject([] { XYChartData({XYSeries("", "S", {})}); });
  Reject([] { XYChartData({XYSeries("s", "S", {{"a", 0, std::numeric_limits<double>::infinity()}})}); });
  Reject([] { CategoryChartData({{"a", "A"}}, {CategorySeries("s", "S", {{"a", "unknown", 1}})}); });
  Reject([] { CategoryChartData({{"a", "A"}}, {CategorySeries("s", "S", {{"a", "a", 1}, {"b", "a", 2}})}); });
  Reject([] { CategoryChartData({{"a", "A"}, {"a", "Again"}}); });
  Reject([] { ScatterChartData({ScatterSeries("s", "S", {{"a", 0, 1, -1}})}); });
  Reject([] { ChartAxis::Linear().Domain({1, 1}); });
  Reject([] { ChartAxis::Category().Domain({0, 1}); });
  Reject([] { ChartAxis::Linear().Ticks({{1}, {0}}); });
  auto data = Categories();
  auto hidden = data.WithSeriesVisible("first", false);
  Check(data.Series()[0].Visible() && !hidden.Series()[0].Visible(), "Visibility must not mutate a shared snapshot");
  Check(data.Series()[0].Samples().data() == hidden.Series()[0].Samples().data(), "Visibility must share immutable samples");
  Check(Access::DataOf(data)->entries[0].index == Access::DataOf(hidden)->entries[0].index, "Visibility must share the index");
  Check(data == data.WithSeriesVisible("first", true), "An unchanged visibility update must preserve identity");
  Reject([&] { static_cast<void>(data.WithSeriesVisible("absent", true)); });
}

void GeometryTests() {
  auto config = BarConfig(Categories());
  config.bar_mode = BarMode::Stacked;
  auto model = BuildModel(config);
  Check(model.series[1][0].lower == 10 && model.series[1][0].upper == 40, "Positive stacks must accumulate");
  Check(model.series[1][2].lower == -5 && model.series[1][2].upper == -20, "Negative stacks must accumulate separately");
  Check(!model.series[0][0].exposed && model.series[1][0].exposed, "Only exposed stack ends can be rounded");
  std::vector<ChartMarkerStyle> markers(2);
  auto geometry = BuildGeometry(config, model, {10, 20, 400, 240}, markers);
  Check(geometry.marks.size() == 6, "Expected every bar segment");
  auto hit = Hit(geometry, geometry.marks[1].point, 8);
  Check(hit.has_value(), "Rendered bars must be hittable");
  config.orientation = ChartOrientation::Horizontal;
  geometry = BuildGeometry(config, model, {10, 20, 400, 240}, markers);
  Check(geometry.marks[0].point.x > geometry.marks[0].baseline.x, "Horizontal positive bars must extend right");
  Check(geometry.marks[2].point.x < geometry.marks[2].baseline.x, "Horizontal negative bars must extend left");
  config.bar_mode = BarMode::PercentStacked;
  Reject([&] { BuildModel(config); });
  config = BarConfig(CategoryChartData({{"a", "A"}, {"b", "B"}}, {
    CategorySeries("s", "S", {{"a", "a", 1}, {"b", "b", 0}}),
    CategorySeries("t", "T", {{"a", "a", 3}, {"b", "b", 0}}),
  }));
  config.bar_mode = BarMode::PercentStacked;
  model = BuildModel(config);
  Check(model.series[0][0].normalized == 0.25 && model.series[1][0].upper == 1, "Percent stacks must normalize visible values");
  Check(model.series[0][1].gap && model.series[1][1].gap, "Zero totals must not produce a division by zero");
  const auto extremes = AutoDomain(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), false);
  Check(std::abs(Fraction(0, extremes) - 0.5) < 1e-12, "Extreme finite domains must map without overflow");
  Check(AutoDomain(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), false).min <
        std::numeric_limits<double>::max(), "A constant extreme domain must expand inward");

  Config line;
  line.data = Access::DataOf(XYChartData({XYSeries("s", "S", {{"a", 0, 1}, {"b", 1, std::nullopt}, {"c", 2, 3}})}));
  auto line_model = BuildModel(line);
  auto lines = BuildGeometry(line, line_model, {0, 0, 400, 200}, std::vector<ChartMarkerStyle>(1));
  Check(lines.contours.size() == 2, "Missing values must split paths");
  std::vector<XYDatum> dense;
  for (int i = 0; i < 100000; ++i) dense.push_back({std::to_string(i), static_cast<double>(i), i == 51321 ? 100.0 : std::sin(i * 0.01)});
  line.data = Access::DataOf(XYChartData({XYSeries("dense", "Dense", std::move(dense))}));
  line_model = BuildModel(line);
  lines = BuildGeometry(line, line_model, {0, 0, 400, 200}, std::vector<ChartMarkerStyle>(1));
  Check(lines.marks.size() <= 1604, "Dense lines must be decimated to pixel density");
  Check(std::any_of(lines.marks.begin(), lines.marks.end(), [](const Mark& m) { return m.sample == 51321; }), "Sampling must retain extrema");
  for (auto interpolation : {ChartInterpolation::Linear, ChartInterpolation::Monotone, ChartInterpolation::StepBefore, ChartInterpolation::StepAfter}) {
    line.interpolation = interpolation;
    lines = BuildGeometry(line, line_model, {0, 0, 400, 200}, std::vector<ChartMarkerStyle>(1));
    Check(!lines.contours.front().line.IsEmpty(), "Each interpolation must produce geometry");
  }
  Config scatter;
  scatter.kind = Kind::Scatter;
  scatter.data = Access::DataOf(ScatterChartData({ScatterSeries("s", "S", {{"a", 0, 0, 0}, {"b", 1, 1, 10}})}));
  const auto bubbles = BuildGeometry(scatter, BuildModel(scatter), {0, 0, 400, 200}, std::vector<ChartMarkerStyle>(1));
  Check(bubbles.marks[0].area == scatter.size_scale.min_area && bubbles.marks[1].area == scatter.size_scale.max_area,
        "Bubble magnitude must map to area");

  auto area = BarConfig(CategoryChartData({{"a", "A"}, {"b", "B"}, {"c", "C"}}, {
    CategorySeries("s", "S", {{"a", "a", 2}, {"b", "b", 3}, {"c", "c", 4}}),
    CategorySeries("t", "T", {{"a", "a", 1}, {"c", "c", 2}}),
  }));
  area.kind = Kind::Area;
  area.area_mode = AreaMode::Stacked;
  auto areas = BuildModel(area);
  Check(areas.series[0][1].gap, "A missing member must break the entire area stack");
  auto area_geometry = BuildGeometry(area, areas, {0, 0, 400, 200}, markers);
  Check(area_geometry.contours.size() == 4, "All area series must break at the missing category");
  area.interpolation = ChartInterpolation::Monotone;
  Reject([&] { BuildModel(area); });

  Config overflow = BarConfig(CategoryChartData({{"a", "A"}}, {
    CategorySeries("s", "S", {{"a", "a", std::numeric_limits<double>::max()}}),
    CategorySeries("t", "T", {{"a", "a", std::numeric_limits<double>::max()}}),
  }));
  overflow.bar_mode = BarMode::Stacked;
  Reject([&] { BuildModel(overflow); });

  auto start = geometry;
  auto end = geometry;
  end.marks[0].point.x += 20;
  auto middle = InterpolateGeometry(start, end, 0.5F, ChartInterpolation::Linear);
  Check(std::abs(middle.marks[0].point.x - start.marks[0].point.x - 10) < 0.001F, "Animation must interpolate matched geometry");
}

void ProjectionTests() {
  const Rect plot{10, 20, 360, 240};
  const ChartMarkerStyle marker{.shape = ChartMarkerShape::Diamond, .area = 144};
  const auto check_projection = [&](const Config& config) {
    const auto model = BuildModel(config);
    const auto& entries = config.data->entries;
    const auto count = static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(),
        [](const Entry& entry) { return entry.visible; }));
    const auto geometry = BuildGeometry(config, model, plot, std::vector<ChartMarkerStyle>(entries.size(), marker));
    for (const auto& mark : geometry.marks) {
      const auto slot = static_cast<std::size_t>(std::count_if(entries.begin(), entries.begin() + mark.series,
          [](const Entry& entry) { return entry.visible; }));
      const auto projected = ProjectMark(config, model, plot, mark.series,
                                          model.series[mark.series][mark.sample], marker, slot, count);
      Check(projected && projected->point == mark.point && projected->baseline == mark.baseline &&
            projected->bounds == mark.bounds && projected->area == mark.area && projected->shape == mark.shape &&
            projected->series == mark.series && projected->sample == mark.sample &&
            projected->exposed == mark.exposed && projected->normalized == mark.normalized &&
            projected->source.x == mark.source.x && projected->source.y == mark.source.y &&
            projected->source_baseline.x == mark.source_baseline.x &&
            projected->source_baseline.y == mark.source_baseline.y,
            "Single-mark projection must preserve full-geometry positions, styles and source metadata");
    }
    return geometry;
  };
  Config config;
  config.sampling = ChartSampling::None;
  config.x = ChartAxis::Linear().Domain({0, 1});
  config.y = ChartAxis::Linear().Domain({0, 1});
  config.data = Access::DataOf(XYChartData({XYSeries("s", "S", {
    {"outside-left", -1, -2}, {"inside", 0.5, 0.75}, {"gap", 0.75, std::nullopt}, {"outside-right", 2, 3},
  })}));
  for (const auto kind : {Kind::Line, Kind::Area}) {
    config.kind = kind;
    config.baseline = 0.25;
    const auto geometry = check_projection(config);
    Check(geometry.marks.size() == 3 && geometry.marks.front().point.x < plot.x &&
          geometry.marks.front().source.y == -2,
          "Connected charts must retain offscreen anchors while omitting gaps");
    const auto model = BuildModel(config);
    Check(!ProjectMark(config, model, plot, 0, model.series[0][2], marker, 0, 1),
          "A gap must not project into an interaction mark");
    Check(!ProjectMark(config, model, {}, 0, model.series[0][1], marker, 0, 1),
          "An empty plot must not project an interaction mark");
  }
  config = BarConfig(CategoryChartData({{"a", "A"}, {"b", "B"}, {"c", "C"}}, {
    CategorySeries("first", "First", {{"c1", "c", -5}, {"a1", "a", 10}}),
    CategorySeries("hidden", "Hidden", {{"a2", "a", 20}}, false),
    CategorySeries("empty", "Empty", {}),
    CategorySeries("last", "Last", {{"a3", "a", 30}, {"c3", "c", -15}}),
  }));
  config.y = ChartAxis::Linear().Domain({-10, 25});
  for (const auto orientation : {ChartOrientation::Vertical, ChartOrientation::Horizontal}) {
    config.orientation = orientation;
    for (const auto mode : {BarMode::Grouped, BarMode::Stacked}) {
      config.bar_mode = mode;
      const auto geometry = check_projection(config);
      Check(geometry.marks.size() == 4, "Projection must preserve partially clipped bars and skip absent series");
      if (mode == BarMode::Grouped) {
        const float origin = orientation == ChartOrientation::Vertical ? plot.x : plot.y;
        const float band = (orientation == ChartOrientation::Vertical ? plot.width : plot.height) / 3;
        const auto position = [&](const Mark& mark) {
          return orientation == ChartOrientation::Vertical ? mark.point.x : mark.point.y;
        };
        Check(std::abs(position(geometry.marks[0]) - origin - band * (0.5F - 0.8F / 3)) < 0.001F &&
              std::abs(position(geometry.marks[2]) - origin - band * (0.5F + 0.8F / 3)) < 0.001F,
              "Grouped projection must retain an empty visible series slot and omit a hidden series slot");
      }
      Check(DatumInfo(*config.data, geometry.marks.front()).datum_index == 1,
            "Projection must preserve original datum indices after category sorting");
    }
  }
  config = BarConfig(CategoryChartData({{"a", "A"}}, {
    CategorySeries("s", "S", {{"a", "a", 1}}), CategorySeries("t", "T", {{"b", "a", 3}}),
  }));
  config.bar_mode = BarMode::PercentStacked;
  Check(check_projection(config).marks.front().normalized == 0.25,
        "Single-mark projection must retain percent-stack values");
  config = Config{};
  config.kind = Kind::Scatter;
  config.data = Access::DataOf(ScatterChartData({ScatterSeries("s", "S", {
    {"zero", 0.2, 0.5, 0}, {"sized", 0.5, 0.5, 50}, {"plain", 0.8, 0.5},
  })}));
  config.size_scale = {.domain = ChartNumericDomain{0, 100}, .min_area = 0, .max_area = 400};
  const auto geometry = check_projection(config);
  Check(geometry.marks.size() == 2 && geometry.marks[0].area == 200 && geometry.marks[1].area == 144,
        "Projection must use magnitude area when present and marker area otherwise, omitting zero-area bubbles");
}

void ClippingTests() {
  Config config;
  config.x = ChartAxis::Linear().Domain({0, 1});
  config.y = ChartAxis::Linear().Domain({0, 1});
  config.sampling = ChartSampling::None;
  const Rect plot{0, 0, 100, 100};
  const std::vector<ChartMarkerStyle> markers(1);
  const auto build = [&](std::vector<XYDatum> samples) {
    config.data = Access::DataOf(XYChartData({XYSeries("s", "S", std::move(samples))}));
    return BuildGeometry(config, BuildModel(config), plot, markers);
  };
  for (const auto interpolation : {ChartInterpolation::Linear, ChartInterpolation::Monotone,
                                   ChartInterpolation::StepBefore, ChartInterpolation::StepAfter}) {
    config.interpolation = interpolation;
    const auto geometry = build({{"a", -100, 0}, {"b", 10, 1}});
    const float expected_y = interpolation == ChartInterpolation::StepBefore ? 0.0F :
        interpolation == ChartInterpolation::StepAfter ? 100.0F : 100.0F * (1.0F - 100.5F / 110.0F);
    const auto hit = Hit(geometry, {50, expected_y}, 0.01F);
    Check(hit.has_value(), "Clipping must preserve the original interpolation across distant x endpoints");
    Check(DatumInfo(*config.data, geometry.marks[*hit]).identity.series_key == "s",
          "A clipped segment must still identify an original datum");
    Check(!Hit(geometry, {50, 50}, 0.01F), "Clipping must not introduce a false diagonal");
    for (const auto& contour : geometry.contours) for (const auto& segment : contour.segments) {
      Check(plot.Contains(segment.start) && plot.Contains(segment.end), "Rendered segments must remain inside the plot");
    }
  }
  config.interpolation = ChartInterpolation::Linear;
  auto geometry = build({{"a", 0, -100}, {"b", 1, 10}});
  Check(Hit(geometry, {100.0F * 100.5F / 110.0F, 50}, 0.01F).has_value(),
        "Clipping must preserve slopes across distant y endpoints");
  geometry = build({{"a", 0, 0.5}, {"b", 0.5, 2}, {"c", 1, 0.5}});
  Check(!Hit(geometry, {50, 0}, 0.01F), "Excursions outside the plot must not create a stroked boundary bridge");
  geometry = build({{"a", -100, 2}, {"b", 10, 3}});
  Check(geometry.contours.front().line.IsEmpty(), "An entirely offscreen line must not be drawn on a boundary");
  geometry = build({{"a", -100, 0.5}, {"gap", 0.5, std::nullopt}, {"b", 10, 0.5}});
  Check(!Hit(geometry, {50, 50}, 0), "Clipping must preserve explicit gaps");
  config.kind = Kind::Area;
  config.style = AreaChartStyle{};
  geometry = build({{"a", -100, 0}, {"b", 10, 1}});
  Check(geometry.contours.front().fill.Contains({50, 20}, PathFillRule::NonZero),
        "Area clipping must retain the visible interior under the true curve");
  Check(!geometry.contours.front().fill.Contains({50, 2}, PathFillRule::NonZero),
        "Area clipping must not fill above the true curve");
  config.baseline = -100;
  geometry = build({{"a", -100, 2}, {"b", 10, 2}});
  Check(geometry.contours.front().fill.Contains({50, 50}, PathFillRule::NonZero),
        "An area spanning the viewport must remain filled even when every vertex is outside");
  config.kind = Kind::Line;
  config.style = LineChartStyle{};
  config.baseline = 0;
  const double extreme = std::numeric_limits<double>::max();
  geometry = build({{"a", -extreme, 0.5}, {"b", extreme, 0.5}});
  Check(Hit(geometry, {50, 50}, 0).has_value(), "Extreme finite endpoints must clip without overflowing");
  config.x = ChartAxis::Linear().Domain({0, 1e-300});
  geometry = build({{"a", -1e300, 0.5}, {"b", 1e300, 0.5}});
  Check(Hit(geometry, {50, 50}, 0).has_value(), "Clipping must precede an otherwise overflowing projection");
}

void MarkerTests() {
  Config config;
  config.kind = Kind::Scatter;
  config.style = ScatterChartStyle{};
  config.data = Access::DataOf(ScatterChartData({ScatterSeries("s", "S", {{"a", 0.5, 0.5}})}));
  config.x = ChartAxis::Linear().Domain({0, 1});
  config.y = ChartAxis::Linear().Domain({0, 1});
  const auto model = BuildModel(config);
  for (const auto shape : {ChartMarkerShape::Circle, ChartMarkerShape::Square, ChartMarkerShape::Diamond,
                          ChartMarkerShape::Triangle, ChartMarkerShape::Cross}) {
    const auto geometry = BuildGeometry(config, model, {0, 0, 200, 200}, {{.shape = shape, .area = 400}});
    const auto& mark = geometry.marks.front();
    const auto path = MarkerPath(mark.point, mark.area, shape);
    const auto bounds = path.Bounds();
    Check(std::abs(mark.bounds.x - bounds.x) < 0.01F && std::abs(mark.bounds.y - bounds.y) < 0.01F &&
          std::abs(mark.bounds.width - bounds.width) < 0.01F && std::abs(mark.bounds.height - bounds.height) < 0.01F,
          "Painted marker bounds and indexed bounds must agree for every shape");
    bool matching = true;
    for (float y = 80.25F; y < 120; y += 1.0F) for (float x = 80.25F; x < 120; x += 1.0F) {
      matching = matching && (Hit(geometry, {x, y}, 0).has_value() ==
                               path.Contains({x, y}, PathFillRule::NonZero));
    }
    Check(matching, "Zero-slop marker hits must match the painted shape, including concave corners");
    const Point edge = shape == ChartMarkerShape::Triangle ? Point{100, bounds.y + bounds.height} :
                                                           Point{bounds.x + bounds.width, 100};
    const Point outside = shape == ChartMarkerShape::Triangle ? Point{edge.x, edge.y + 2} : Point{edge.x + 2, edge.y};
    Check(Hit(geometry, edge, 0.001F).has_value(), "Marker boundary points must be hittable");
    Check(!Hit(geometry, outside, 1) && Hit(geometry, outside, 3),
          "Hit slop must expand the actual marker boundary by the requested distance");
    auto larger = BuildGeometry(config, model, {0, 0, 200, 200}, {{.shape = shape, .area = 1600}});
    auto middle = InterpolateGeometry(geometry, larger, 0.5F, ChartInterpolation::Linear);
    Check(middle.marks[0].bounds == MarkerBounds(middle.marks[0].point, middle.marks[0].area, shape),
          "Animated hit bounds must follow interpolated area, not interpolated width");
  }
}

class TestResources final : public PlatformResources {
public:
  ResourceConfiguration configuration;
  ResourceConfiguration Configuration() const override { return configuration; }
  RawAsset Read(std::string_view relative_path) override {
    std::ifstream input(std::filesystem::path(CHARTS_TEST_PACKAGE_ROOT) / relative_path, std::ios::binary);
    if (!input) return {};
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return RawAsset::CopyBytes(std::as_bytes(std::span(bytes)));
  }
};

class TestPlatform final : public PlatformAdapter {
public:
  TestResources resources;
  PlatformResources* Resources() noexcept override { return &resources; }
  double now = 0;
  std::size_t measurements = 0;
  void RequestFrameAt(double) override {}
  double Now() const noexcept override { return now; }
  FontMetrics Metrics(const Font&) override { return {.ascent = 10, .descent = 3, .leading = 1}; }
  TextRunMetrics MeasureRun(std::string_view text, const TextStyle&, const TextShapingOptions&) override {
    ++measurements;
    return {.advance = static_cast<float>(text.size()) * 7, .visual_bounds = {0, -10, static_cast<float>(text.size()) * 7, 14},
            .font_metrics = {.ascent = 10, .descent = 3, .leading = 1}};
  }
  TextLayoutMetrics MeasureText(std::string_view text, const TextStyle&, float width, const TextLayoutOptions&) override {
    ++measurements;
    return {.size = {std::min(width, static_cast<float>(text.size()) * 7), 14}, .first_baseline = 10, .last_baseline = 10, .line_count = 1};
  }
};

Kind legend_kind = Kind::Line;
int legend_layer = 0;
int visibility_requests = 0;
std::optional<State<int>> legend_revision;

Brush LegendBrush(int layer) {
  if (layer == 0) return Color::Rgb(200, 30, 60);
  if (layer == 1) return Color::Rgb(30, 160, 90);
  return LinearGradient{.stops = {{0, Color::Rgb(20, 80, 240)}, {1, Color::Rgb(240, 130, 20)}}};
}

StrokeStyle LegendStroke() { return {.width = 3.0F, .dash_pattern = {4.0F, 2.0F}}; }

View LegendApp() {
  const auto revision = UseState(0);
  legend_revision = revision;
  static_cast<void>(revision.Get());
  const auto xy = XYChartData({XYSeries("s", "Series", {{"a", 0, 0.25}, {"b", 1, 0.75}})});
  const auto categories = CategoryChartData({{"a", "A"}}, {CategorySeries("s", "Series", {{"a", "a", 1}})});
  const auto scatter = ScatterChartData({ScatterSeries("s", "Series", {{"a", 0.5, 0.5}})});
  const auto marker = [](int layer) {
    return ChartMarkerStyle{.shape = ChartMarkerShape::Triangle, .visible = true, .area = 64.0F,
                            .fill = LegendBrush(layer), .stroke = Brush(Color::Black()), .stroke_style = LegendStroke()};
  };
  ChartTheme theme;
  theme.line = {.stroke = LegendBrush(0), .stroke_style = LegendStroke()};
  theme.area = {.fill = LegendBrush(0), .stroke = Brush(Color::Black()), .stroke_style = LegendStroke()};
  theme.bar = {.fill = LegendBrush(0), .stroke = Brush(Color::Black()), .stroke_style = LegendStroke(), .corner_radius = 1};
  theme.scatter = {.marker = marker(0)};
  View chart;
  switch (legend_kind) {
    case Kind::Line: {
      auto line = LineChart(xy);
      if (legend_layer >= 1) line = std::move(line).Style({.stroke = LegendBrush(1)});
      if (legend_layer >= 2) line = std::move(line).SeriesStyle("s", {.stroke = LegendBrush(2)});
      chart = std::move(line).Tooltip({.enabled = false});
      break;
    }
    case Kind::Area: {
      auto area = AreaChart(xy);
      if (legend_layer >= 1) area = std::move(area).Style({.fill = LegendBrush(1)});
      if (legend_layer >= 2) area = std::move(area).SeriesStyle("s", {.fill = LegendBrush(2)});
      chart = std::move(area).Tooltip({.enabled = false});
      break;
    }
    case Kind::Bar: {
      auto bar = BarChart(categories.WithSeriesVisible("s", false));
      if (legend_layer >= 1) bar = std::move(bar).Style({.fill = LegendBrush(1)});
      if (legend_layer >= 2) bar = std::move(bar).SeriesStyle("s", {.fill = LegendBrush(2)});
      chart = std::move(bar).Tooltip({.enabled = false});
      break;
    }
    case Kind::Scatter: {
      auto points = ScatterChart(scatter);
      if (legend_layer >= 1) points = std::move(points).Style({.marker = marker(1)});
      if (legend_layer >= 2) points = std::move(points).SeriesStyle("s", {.marker = marker(2)});
      chart = std::move(points).Tooltip({.enabled = false});
      break;
    }
  }
  return FlatTheme {
    ProvideEnvironment(theme, std::move(chart)
        .On<ChartEvents::SeriesVisibilityRequested>([](const ChartSeriesVisibilityRequest& request) {
          if (request.series_key == "s") ++visibility_requests;
        })
        .With(Frame{.width = 400, .height = 300})),
  };
}

void LegendTests() {
  for (const auto kind : {Kind::Line, Kind::Area, Kind::Bar, Kind::Scatter}) {
    legend_kind = kind;
    legend_layer = 0;
    TestPlatform platform;
    const Application app{LegendApp};
    Runtime runtime(app, platform);
    runtime.SetWindowMetrics({.viewport = {400, 300}});
    for (int layer = 0; layer < 3; ++layer) {
      legend_layer = layer;
      if (layer > 0) *legend_revision = layer;
      const auto frame = runtime.BuildFrame();
      std::vector<PaintCommand> swatch;
      const auto visit = [&](auto&& self, const RenderNode* node) -> void {
        if (!node) return;
        for (const auto& command : node->content.Commands()) {
          const auto* clip = std::get_if<PushClipCommand>(&command);
          if (clip && clip->rect == Rect{0, 0, 24, 16}) swatch = node->content.Commands();
        }
        for (const auto* child : node->children) self(self, child);
      };
      visit(visit, frame.render_frame.scene.root);
      Check(!swatch.empty(), "Every visible legend must contain a styled swatch");
      bool paint_matches = false;
      bool stroke_matches = false;
      bool shape_matches = kind != Kind::Scatter;
      for (const auto& command : swatch) {
        if (const auto* fill = std::get_if<FillPathCommand>(&command)) {
          if (kind != Kind::Line && fill->brush == LegendBrush(layer)) paint_matches = true;
          if (kind == Kind::Scatter && fill->path == MarkerPath({12, 8}, 48, ChartMarkerShape::Triangle)) shape_matches = true;
        }
        if (const auto* stroke = std::get_if<StrokePathCommand>(&command)) {
          if (kind == Kind::Line && stroke->brush == LegendBrush(layer)) paint_matches = true;
          if (stroke->style == LegendStroke()) stroke_matches = true;
        }
      }
      Check(paint_matches, "Legend brushes must follow theme, chart and keyed series precedence, including gradients");
      Check(stroke_matches, "Legend stroke width and dash pattern must follow the resolved series style");
      Check(shape_matches, "Scatter legend symbols must use the resolved marker shape");
      if (kind == Kind::Bar) {
        const auto checkbox = std::find_if(frame.semantic_frame->nodes.begin(), frame.semantic_frame->nodes.end(),
            [](const SemanticNode& node) { return node.role == SemanticRole::Checkbox; });
        Check(checkbox != frame.semantic_frame->nodes.end() && checkbox->checked == SemanticCheckedState::Unchecked,
              "A hidden series must retain its styled, unchecked legend entry");
        const int before = visibility_requests;
        runtime.PerformSemanticAction(checkbox->id, {SemanticActionKind::Activate});
        Check(visibility_requests == before + 1, "Styled legends must preserve visibility request routing");
      }
    }
    legend_revision.reset();
  }
}

const Color highlight_color = Color::Rgb(195, 35, 145, 0.8F);
const Color highlight_mark_color = Color::Rgb(20, 140, 130);
Kind highlight_kind = Kind::Scatter;
ChartMarkerShape highlight_shape = ChartMarkerShape::Circle;
ChartOrientation highlight_orientation = ChartOrientation::Vertical;
BarMode highlight_bar_mode = BarMode::Grouped;
bool highlight_marker_visible = true;
bool highlight_negative = false;
bool highlight_boundary = false;
bool highlight_dark = false;
bool highlight_moved = false;
float highlight_area = 100.0F;
float highlight_width = 2.0F;
std::optional<State<int>> highlight_revision;
std::optional<State<std::optional<ChartDatumIdentity>>> highlight_selection;

View HighlightApp() {
  const auto revision = UseState(0);
  highlight_revision = revision;
  static_cast<void>(revision.Get());
  const auto selected = UseState(std::optional<ChartDatumIdentity>{});
  highlight_selection = selected;
  static const XYChartData xy({XYSeries("s", "Series", {{"a", 0.4, 0.5}, {"b", 0.7, 0.7}})});
  static const ScatterChartData scatter({ScatterSeries("s", "Series", {{"a", 0.4, 0.5}, {"b", 0.7, 0.7}})});
  static const ScatterChartData boundary({ScatterSeries("s", "Series", {{"a", 0.0, 0.5}, {"b", 0.7, 0.7}})});
  static const ScatterChartData moved({ScatterSeries("s", "Series", {{"a", 0.6, 0.8}, {"b", 0.8, 0.9}})});
  static const CategoryChartData bars({{"a", "A"}}, {
    CategorySeries("s", "Series", {{"a", "a", 0.4}}),
    CategorySeries("t", "Other", {{"a", "a", 0.3}}),
  });
  static const CategoryChartData negative({{"a", "A"}}, {
    CategorySeries("s", "Series", {{"a", "a", -0.4}}),
    CategorySeries("t", "Other", {{"a", "a", -0.3}}),
  });
  const ChartMarkerStyle marker{
      .shape = highlight_shape, .visible = highlight_marker_visible, .area = highlight_area,
      .fill = Brush(highlight_mark_color), .stroke = Brush(highlight_mark_color),
      .stroke_style = StrokeStyle{.width = 2.0F},
  };
  const auto finish = [selected](auto chart) -> View {
    return std::move(chart)
        .Appearance({.interaction = {.highlight = highlight_color, .width = highlight_width}})
        .Legend({.visible = false})
        .Tooltip({.enabled = false})
        .Selection(selected.Get())
        .template On<ChartEvents::SelectionRequested>([selected](const ChartDatumInfo& info) { selected = info.identity; })
        .With(Frame{.width = 400.0F, .height = 300.0F});
  };
  View chart;
  switch (highlight_kind) {
    case Kind::Line:
      chart = finish(LineChart(xy).XAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
          .YAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
          .Style({.stroke = Brush(highlight_mark_color), .marker = marker}));
      break;
    case Kind::Area:
      chart = finish(AreaChart(xy).XAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
          .YAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
          .Style({.stroke = Brush(highlight_mark_color), .marker = marker}));
      break;
    case Kind::Bar:
      chart = finish(BarChart(highlight_negative ? negative : bars)
          .ValueAxis(ChartAxis::Linear().Domain({-1, 1}).Visible(false))
          .CategoryAxis(ChartAxis::Category().Visible(false))
          .Orientation(highlight_orientation).Mode(highlight_bar_mode)
          .Style({.fill = Brush(highlight_mark_color), .corner_radius = 10.0F}));
      break;
    case Kind::Scatter:
      chart = finish(ScatterChart(highlight_boundary ? boundary : highlight_moved ? moved : scatter)
          .XAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
          .YAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
          .Animation({.enabled = highlight_moved}).Style({.marker = marker}));
      break;
  }
  return FlatTheme(highlight_dark ? FlatDarkThemeSpec() : FlatLightThemeSpec(), Column {
    chart,
    Button("Outside"),
  });
}

struct HighlightPaint {
  Rect clip;
  std::vector<StrokePathCommand> rings;
  std::vector<StrokePathCommand> strokes;
  std::vector<FillPathCommand> anchors;
  std::vector<FillPathCommand> marks;
};

HighlightPaint ReadHighlights(Runtime& runtime) {
  HighlightPaint result;
  const auto visit = [&](auto&& self, const RenderNode* node) -> void {
    if (!node) return;
    for (const auto& command : node->content.Commands()) {
      if (const auto* fill = std::get_if<FillPathCommand>(&command)) {
        if (fill->brush == Brush(highlight_mark_color)) result.marks.push_back(*fill);
      }
    }
    for (const auto& command : node->foreground.Commands()) {
      if (const auto* clip = std::get_if<PushClipCommand>(&command)) result.clip = clip->rect;
      if (const auto* fill = std::get_if<FillPathCommand>(&command)) result.anchors.push_back(*fill);
      if (const auto* stroke = std::get_if<StrokePathCommand>(&command)) {
        result.strokes.push_back(*stroke);
        auto hover_color = highlight_color;
        hover_color.alpha *= 0.45F;
        if (stroke->brush == Brush(highlight_color) || stroke->brush == Brush(hover_color)) {
          result.rings.push_back(*stroke);
        }
      }
    }
    for (const auto* child : node->children) self(self, child);
  };
  visit(visit, runtime.BuildFrame().render_frame.scene.root);
  return result;
}

void HighlightGeometryTests() {
  for (const auto kind : {Kind::Line, Kind::Area, Kind::Scatter}) {
    highlight_kind = kind;
    for (const auto shape : {ChartMarkerShape::Circle, ChartMarkerShape::Square, ChartMarkerShape::Diamond,
                            ChartMarkerShape::Triangle, ChartMarkerShape::Cross}) {
      highlight_shape = shape;
      TestPlatform platform;
      const Application app{HighlightApp};
      Runtime runtime(app, platform);
      runtime.SetWindowMetrics({.viewport = {440, 360}});
      runtime.BuildFrame();
      *highlight_selection = ChartDatumIdentity{"s", "a"};
      const auto paint = ReadHighlights(runtime);
      Check(paint.rings.size() == 1, "Every point shape must produce exactly one selection ring");
      Check(paint.marks.size() == 2, "Visible markers must retain their original geometry");
      const Point center{paint.clip.x + paint.clip.width * 0.4F, paint.clip.y + paint.clip.height * 0.5F};
      const auto bounds = MarkerBounds({}, 1.0F, shape);
      const double extent = std::sqrt(static_cast<double>(highlight_area)) + 8.0 / std::min(bounds.width, bounds.height);
      const auto expected = MarkerPath(center, static_cast<float>(extent * extent), shape);
      Check(paint.rings.front().path == expected,
            "Selection rings must follow marker shape instead of its bounding box");
      Check(paint.marks.front().path == MarkerPath(center, highlight_area, shape),
            "A highlight must not resize the underlying marker");
    }
    highlight_marker_visible = false;
    TestPlatform platform;
    const Application app{HighlightApp};
    Runtime runtime(app, platform);
    runtime.SetWindowMetrics({.viewport = {440, 360}});
    runtime.BuildFrame();
    *highlight_selection = ChartDatumIdentity{"s", "a"};
    const auto paint = ReadHighlights(runtime);
    const Point center{paint.clip.x + paint.clip.width * 0.4F, paint.clip.y + paint.clip.height * 0.5F};
    Check(paint.anchors.size() == 1 && paint.anchors.front().path == MarkerPath(center, 36, ChartMarkerShape::Circle),
          "A hidden marker must gain a circular datum anchor, independent of the hidden marker shape");
    highlight_marker_visible = true;
  }
  highlight_kind = Kind::Bar;
  for (const auto orientation : {ChartOrientation::Vertical, ChartOrientation::Horizontal}) {
    highlight_orientation = orientation;
    for (const auto mode : {BarMode::Grouped, BarMode::Stacked, BarMode::PercentStacked}) {
      highlight_bar_mode = mode;
      for (const bool negative : {false, true}) {
        if (negative && mode == BarMode::PercentStacked) continue;
        highlight_negative = negative;
        TestPlatform platform;
        const Application app{HighlightApp};
        Runtime runtime(app, platform);
        runtime.SetWindowMetrics({.viewport = {440, 360}});
        runtime.BuildFrame();
        for (const auto* series : {"s", "t"}) {
          *highlight_selection = ChartDatumIdentity{series, "a"};
          const auto paint = ReadHighlights(runtime);
          Check(paint.rings.size() == 1 && paint.marks.size() == 2, "Bars must have a single selection outline");
          const auto index = std::string_view(series) == "s" ? 0 : 1;
          Check(paint.rings.front().path == paint.marks[index].path,
                "Bar highlights must preserve sign, orientation, corner radius and internal stack boundaries");
          CornerRadii corners;
          if (mode == BarMode::Grouped || index == 1) {
            if (orientation == ChartOrientation::Horizontal) corners = negative ?
                CornerRadii{10, 0, 0, 10} : CornerRadii{0, 10, 10, 0};
            else corners = negative ? CornerRadii{0, 0, 10, 10} : CornerRadii{10, 10, 0, 0};
          }
          Check(paint.rings.front().path == Path::RoundedRect(paint.marks[index].path.Bounds(), corners),
                "Only exposed bar ends may be rounded");
        }
      }
    }
  }
  highlight_negative = false;
  highlight_kind = Kind::Scatter;
  highlight_shape = ChartMarkerShape::Circle;
  highlight_revision.reset();
  highlight_selection.reset();
}

void HighlightInteractionTests() {
  for (const bool dark : {false, true}) {
    highlight_dark = dark;
    TestPlatform platform;
    const Application app{HighlightApp};
    Runtime runtime(app, platform);
    runtime.SetWindowMetrics({.viewport = {440, 360}});
    auto paint = ReadHighlights(runtime);
    Check(paint.rings.empty(), "An idle chart must not draw data highlights");
    const Point point{paint.clip.x + paint.clip.width * 0.4F, paint.clip.y + paint.clip.height * 0.5F};
    runtime.HandlePointerEvent({.type = PointerEventType::Move, .position = point});
    paint = ReadHighlights(runtime);
    auto hover_color = highlight_color;
    hover_color.alpha *= 0.45F;
    Check(paint.rings.size() == 1 && paint.rings.front().brush == Brush(hover_color),
          "Hover must use a softer ring without committing selection");
    Check(!highlight_selection->Get(), "Hover must leave controlled selection untouched");
    runtime.HandlePointerEvent({.type = PointerEventType::Down, .position = point,
                               .changed_button = PointerButton::Primary, .pressed_buttons = PointerButton::Primary});
    runtime.HandlePointerEvent({.type = PointerEventType::Up, .position = point,
                               .changed_button = PointerButton::Primary});
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 1 && paint.rings.front().brush == Brush(highlight_color),
          "Selection and hover on one datum must not double-paint a ring");
    Check(paint.strokes.size() == 1, "Pointer focus must not add a keyboard underlay");
    Check(runtime.HandleKeyEvent({.key = Key::Home}), "Keyboard navigation must establish its own focus treatment");
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 1 && paint.strokes.size() == 2,
          "Keyboard focus, hover and selection must resolve to one contrasted ring");
    const auto ring = paint.rings.front();
    const auto surface = dark ? FlatDarkThemeSpec().colors.surface : FlatLightThemeSpec().colors.surface;
    Check(paint.strokes.front().path == ring.path && paint.strokes.front().brush == Brush(surface) &&
          paint.strokes.front().style.width > ring.style.width,
          "Keyboard underlay must follow the active theme and ring");
    runtime.HandlePointerEvent({.type = PointerEventType::Down, .position = point,
                               .changed_button = PointerButton::Primary, .pressed_buttons = PointerButton::Primary});
    runtime.HandlePointerEvent({.type = PointerEventType::Up, .position = point,
                               .changed_button = PointerButton::Primary});
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 1 && paint.strokes.size() == 1,
          "Returning to pointer input must remove the keyboard-only underlay");
    runtime.HandleKeyEvent({.key = Key::Home});
    runtime.HandlePointerEvent({.type = PointerEventType::Move,
        .position = {paint.clip.x + paint.clip.width * 0.7F, paint.clip.y + paint.clip.height * 0.3F}});
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 2, "Hover on another datum must coexist with the selected keyboard target");
    Check(runtime.HandleKeyEvent({.key = Key::Escape}), "Escape must clear keyboard highlighting");
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 2 && paint.strokes.size() == 2, "Escape must preserve selection and independent hover");
    runtime.HandlePointerEvent({.type = PointerEventType::Move, .position = {430, 350}});
    Check(ReadHighlights(runtime).rings.size() == 1, "Hover leave must preserve the controlled selection ring");
    runtime.HandleKeyEvent({.key = Key::Home});
    const auto frame = runtime.BuildFrame().semantic_frame;
    const auto outside = std::find_if(frame->nodes.begin(), frame->nodes.end(), [](const auto& node) {
      return node.label == "Outside" && node.role == SemanticRole::Button;
    });
    Check(outside != frame->nodes.end() && runtime.PerformSemanticAction(outside->id, {SemanticActionKind::Focus}),
          "Focus must transfer to another control");
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 1 && paint.strokes.size() == 1, "Focus loss must remove only the keyboard treatment");
    highlight_width = 0;
    highlight_marker_visible = false;
    ++*highlight_revision;
    paint = ReadHighlights(runtime);
    Check(paint.rings.empty() && paint.anchors.empty(), "Zero highlight width must suppress rings and anchors");
    highlight_marker_visible = true;
    highlight_width = 2;
    highlight_boundary = true;
    ++*highlight_revision;
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 1 && paint.rings.front().path.Bounds().x < paint.clip.x,
          "Boundary markers must keep their true centered outline and rely on the plot clip");
    highlight_boundary = false;
    highlight_kind = Kind::Line;
    highlight_shape = ChartMarkerShape::Cross;
    highlight_area = std::numeric_limits<float>::denorm_min();
    ++*highlight_revision;
    paint = ReadHighlights(runtime);
    Check(paint.rings.size() == 1 && paint.rings.front().path.Bounds().width < 20,
          "A subpixel marker must receive a finite local ring instead of a viewport-sized outline");
    highlight_area = 100;
    highlight_shape = ChartMarkerShape::Circle;
    highlight_kind = Kind::Scatter;
    ++*highlight_revision;
    const auto before = ReadHighlights(runtime).rings.front().path.Bounds();
    highlight_moved = true;
    highlight_area = 400;
    ++*highlight_revision;
    ReadHighlights(runtime);
    for (int i = 0; i < 5; ++i) {
      platform.now += 0.04;
      paint = ReadHighlights(runtime);
      Check(paint.rings.size() == 1 && paint.marks.size() == 2,
            "Animated markers must retain one selection outline");
      const auto marker = paint.marks.front().path.Bounds();
      const auto ring_bounds = paint.rings.front().path.Bounds();
      Check(std::abs(marker.x + marker.width / 2 - ring_bounds.x - ring_bounds.width / 2) < 0.001F &&
            std::abs(marker.y + marker.height / 2 - ring_bounds.y - ring_bounds.height / 2) < 0.001F &&
            std::abs(ring_bounds.width - marker.width - 8.0F) < 0.001F,
            "Animated ring geometry must follow the current marker position and size");
    }
    Check(paint.rings.front().path.Bounds().x > before.x, "The animation must actually move the highlighted marker");
    highlight_moved = false;
    highlight_area = 100;
  }
  highlight_dark = false;
  highlight_revision.reset();
  highlight_selection.reset();
}

int selected_first = 0;
int selected_second = 0;
std::optional<ChartDatumInfo> last_selected;
std::optional<State<int>> revision;
std::optional<ChartDatumIdentity> controlled_selection;
bool controlled_enabled = true;
bool controlled_mounted = true;
bool controlled_tooltip = false;
bool controlled_animation = false;
bool controlled_dense = false;
int controlled_events = 0;
int callback_generation = -1;
int tooltip_builds = 0;

[[huxerui::composable]] View ControlledChart() {
  const auto generation = UseState(0);
  revision = generation;
  const auto current = generation.Get();
  if (!controlled_mounted) return Text("Unmounted");
  static const auto data = [] {
    std::vector<XYDatum> values;
    for (int i = 0; i < 10000; ++i) values.push_back({std::to_string(i), static_cast<double>(i), std::sin(i * 0.1) + 2});
    return XYChartData({XYSeries("dense", "Dense", std::move(values))});
  }();
  const auto small = XYChartData({XYSeries("dense", "Dense", {{"0", 0, 1.0 + current}, {"1", 1, 2.0 + current}, {"2", 2, 3.0 + current}})});
  return LineChart(controlled_dense ? data : small)
      .Title("Controlled")
      .Legend({.visible = false})
      .Tooltip({.enabled = controlled_tooltip, .content = [](const ChartTooltipContext& context) {
        ++tooltip_builds;
        return Text("Detail " + context.datum.identity.datum_key);
      }})
      .Animation({.enabled = controlled_animation})
      .Accessibility({.max_data_nodes = 8})
      .Selection(controlled_selection)
      .On<ChartEvents::SelectionRequested>([current](const ChartDatumInfo& info) {
        ++controlled_events;
        callback_generation = current;
        last_selected = info;
      })
      .With(Enabled(controlled_enabled), Frame{.width = 480, .height = 300}, Padding(12.0F));
}

View ControlledApp() { return FlatTheme {ControlledChart()}; }

void Recompose() { *revision = revision->Get() + 1; }

void ControlledTests() {
  TestPlatform platform;
  const Application app{ControlledApp};
  Runtime runtime(app, platform);
  runtime.SetWindowMetrics({.viewport = {500, 320}});
  auto get_nodes = [&] {
    std::vector<SemanticNode> nodes;
    for (const auto& node : runtime.BuildFrame().semantic_frame->nodes) if (node.collection_item) nodes.push_back(node);
    return nodes;
  };
  auto nodes = get_nodes();
  Check(nodes.size() == 3, "Controlled chart must expose its data");
  const auto stable_id = nodes[1].id;
  Check(runtime.PerformSemanticAction(stable_id, {SemanticActionKind::Activate}), "Controlled semantic activation must work");
  nodes = get_nodes();
  Check(std::none_of(nodes.begin(), nodes.end(), [](const auto& node) { return node.selected.value_or(false); }),
        "A selection request must not mutate the controlled selection");
  controlled_selection = last_selected->identity;
  Recompose();
  nodes = get_nodes();
  Check(std::any_of(nodes.begin(), nodes.end(), [&](const auto& node) { return node.id == stable_id && node.selected.value_or(false); }),
        "A controlled selection update must retain semantic identity");
  Check(runtime.PerformSemanticAction(stable_id, {SemanticActionKind::Activate}), "Updated semantic node must remain actionable");
  Check(callback_generation == 1, "The latest event handler must replace the old closure");
  const Point point{nodes[1].bounds.x + nodes[1].bounds.width / 2, nodes[1].bounds.y + nodes[1].bounds.height / 2};
  runtime.HandlePointerEvent({.type = PointerEventType::Down, .pointer_id = 5, .position = point,
                             .changed_button = PointerButton::Primary, .pressed_buttons = PointerButton::Primary});
  runtime.HandlePointerEvent({.type = PointerEventType::Up, .pointer_id = 5, .position = point,
                             .changed_button = PointerButton::Primary});
  Check(runtime.HandleKeyEvent({.key = Key::Home}), "Home must establish the chart cursor");
  Check(runtime.HandleKeyEvent({.key = Key::ArrowRight}), "ArrowRight must advance the chart cursor");
  Check(runtime.HandleKeyEvent({.key = Key::Enter}), "Enter must activate the chart cursor");
  Check(last_selected->identity.datum_key == "1", "Keyboard navigation must target original sample identities");
  controlled_tooltip = true;
  Recompose();
  get_nodes();
  runtime.BuildFrame();
  runtime.PerformSemanticAction(stable_id, {SemanticActionKind::Activate});
  runtime.BuildFrame();
  Check(tooltip_builds > 0, "Tooltip content must be composed through the SDK popup");
  controlled_enabled = false;
  Recompose();
  nodes = get_nodes();
  Check(!runtime.PerformSemanticAction(stable_id, {SemanticActionKind::Activate}), "Disabled data must reject semantic actions");
  Check(std::all_of(nodes.begin(), nodes.end(), [](const auto& node) { return !node.enabled; }), "Disabled state must reach virtual children");
  controlled_enabled = true;
  controlled_tooltip = false;
  controlled_animation = true;
  Recompose();
  get_nodes();
  for (int i = 0; i < 20; ++i) { platform.now += 0.02; runtime.BuildFrame(); }
  nodes = get_nodes();
  Check(nodes.size() == 3, "Animation must preserve bounded actionable semantics");
  controlled_dense = true;
  controlled_animation = false;
  controlled_selection.reset();
  Recompose();
  nodes = get_nodes();
  Check(nodes.size() <= 8, "Dense chart semantics must remain bounded");
  Check(runtime.PerformSemanticAction(nodes.front().id, {SemanticActionKind::Focus}), "Dense datum must accept semantic focus");
  runtime.HandleKeyEvent({.key = Key::Home});
  runtime.HandleKeyEvent({.key = Key::ArrowRight});
  runtime.HandleKeyEvent({.key = Key::Enter});
  Check(last_selected->identity.datum_key == "1", "Dense keyboard navigation must not skip decimated data");
  controlled_mounted = false;
  Recompose();
  Check(get_nodes().empty(), "Unmount must remove all virtual children");
  Check(!runtime.PerformSemanticAction(stable_id, {SemanticActionKind::Activate}), "Unmounted semantic identities must be stale");
  revision.reset();
}

ScatterSampling navigation_sampling = ScatterSampling::Exact;

View ScatterNavigationApp() {
  static const ScatterChartData data({ScatterSeries("s", "Series", {
    {"center", 0.5, 0.5, 100},
    {"empty-right", 0.501, 0.5, 0}, {"right", 0.502, 0.5, 100},
    {"empty-left", 0.499, 0.5, 0}, {"left", 0.498, 0.5, 100},
    {"empty-up", 0.5, 0.501, 0}, {"up", 0.5, 0.502, 100},
    {"empty-down", 0.5, 0.499, 0}, {"down", 0.5, 0.498, 100},
  })});
  return FlatTheme {
    ScatterChart(data)
        .XAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
        .YAxis(ChartAxis::Linear().Domain({0, 1}).Visible(false))
        .SizeScale({.domain = ChartNumericDomain{0, 100}, .min_area = 0, .max_area = 100})
        .Sampling(navigation_sampling)
        .Legend({.visible = false})
        .Tooltip({.enabled = false})
        .On<ChartEvents::SelectionRequested>([](const ChartDatumInfo& info) { last_selected = info; })
        .With(Frame{.width = 400, .height = 300}),
  };
}

void ScatterNavigationTests() {
  for (const auto sampling : {ScatterSampling::Exact, ScatterSampling::Representative}) {
    navigation_sampling = sampling;
    TestPlatform platform;
    const Application app{ScatterNavigationApp};
    Runtime runtime(app, platform);
    runtime.SetWindowMetrics({.viewport = {400, 300}});
    const auto frame = runtime.BuildFrame().semantic_frame;
    const auto center = std::find_if(frame->nodes.begin(), frame->nodes.end(),
        [](const SemanticNode& node) { return node.collection_item.has_value(); });
    Check(center != frame->nodes.end(), "Scatter navigation must expose the starting datum");
    const auto count = std::count_if(frame->nodes.begin(), frame->nodes.end(),
        [](const SemanticNode& node) { return node.collection_item.has_value(); });
    Check(sampling == ScatterSampling::Exact ? count == 5 : count < 5,
          "The navigation fixture must omit zero-area bubbles and exercise display reduction");
    const Point point{center->bounds.x + center->bounds.width / 2, center->bounds.y + center->bounds.height / 2};
    runtime.HandlePointerEvent({.type = PointerEventType::Down, .position = point,
                               .changed_button = PointerButton::Primary, .pressed_buttons = PointerButton::Primary});
    runtime.HandlePointerEvent({.type = PointerEventType::Up, .position = point,
                               .changed_button = PointerButton::Primary});
    for (const auto& [key, expected] : {std::pair{Key::ArrowRight, "right"}, {Key::ArrowLeft, "left"},
                                      {Key::ArrowUp, "up"}, {Key::ArrowDown, "down"}}) {
      Check(runtime.PerformSemanticAction(center->id, {SemanticActionKind::Focus}),
            "Each directional check must start at the center datum");
      Check(runtime.HandleKeyEvent({.key = key}), "Scatter navigation must skip zero-area candidates");
      last_selected.reset();
      Check(runtime.HandleKeyEvent({.key = Key::Enter}), "The navigated scatter datum must remain actionable");
      Check(last_selected && last_selected->identity.datum_key == expected,
            "Scatter navigation must reach the nearest visible original datum, including sampled-out points");
      runtime.BuildFrame();
    }
  }
}

View TestApp() {
  return FlatTheme {
    Row {
      BarChart(Categories()).Title("First chart").Legend({.visible = false}).Tooltip({.enabled = false})
          .On<ChartEvents::SelectionRequested>([](const ChartDatumInfo& info) { ++selected_first; last_selected = info; })
          .With(Frame{.width = 400, .height = 300}),
      BarChart(Categories()).Title("Second chart").Legend({.visible = false}).Tooltip({.enabled = false})
          .On<ChartEvents::SelectionRequested>([](const ChartDatumInfo&) { ++selected_second; })
          .With(Frame{.width = 400, .height = 300}),
    },
  };
}

void RuntimeTests() {
  TestPlatform platform;
  const Application app{TestApp};
  Runtime runtime(app, platform);
  runtime.SetWindowMetrics({.viewport = {800, 320}});
  auto frame = runtime.BuildFrame().semantic_frame;
  Check(frame != nullptr, "Runtime must produce semantics");
  std::vector<SemanticNode> data_nodes;
  for (const auto& node : frame->nodes) if (node.collection_item) data_nodes.push_back(node);
  Check(data_nodes.size() == 12, "Both charts must expose their data independently");
  for (const auto& node : data_nodes) Check(node.bounds.width > 0 && node.bounds.height > 0, "Semantic hit bounds must be nonempty");
  Check(runtime.PerformSemanticAction(data_nodes.front().id, {SemanticActionKind::Activate}), "Semantic activation must succeed");
  Check(selected_first == 1 && selected_second == 0, "Selection events must reach only the owning chart");
  Check(last_selected && last_selected->identity.series_key == "first", "Selection must preserve source identity");
  Check(runtime.PerformSemanticAction(data_nodes.back().id, {SemanticActionKind::Activate}), "Second chart activation must succeed");
  Check(selected_first == 1 && selected_second == 1, "Second chart must have an independent emitter");
  const auto first = data_nodes.front().bounds;
  const Point point{first.x + first.width / 2, first.y + first.height / 2};
  runtime.HandlePointerEvent({.type = PointerEventType::Down, .pointer_id = 1, .position = point,
                             .changed_button = PointerButton::Primary, .pressed_buttons = PointerButton::Primary});
  Check(selected_first == 1, "Pointer Down must not select");
  runtime.HandlePointerEvent({.type = PointerEventType::Up, .pointer_id = 1, .position = point,
                             .changed_button = PointerButton::Primary});
  Check(selected_first == 2, "Successful primary Up must select");
  runtime.HandlePointerEvent({.type = PointerEventType::Down, .pointer_id = 2, .position = point,
                             .changed_button = PointerButton::Primary, .pressed_buttons = PointerButton::Primary});
  runtime.HandlePointerEvent({.type = PointerEventType::Cancel, .pointer_id = 2, .position = point});
  runtime.HandlePointerEvent({.type = PointerEventType::Up, .pointer_id = 2, .position = point,
                             .changed_button = PointerButton::Primary});
  Check(selected_first == 2, "Canceled sequences must not select");
  runtime.BuildFrame();
  const auto measurements = platform.measurements;
  platform.now += 0.02;
  runtime.BuildFrame();
  Check(platform.measurements == measurements, "An unchanged frame must not measure labels again");
  runtime.SetWindowMetrics({.viewport = {80, 60}});
  runtime.BuildFrame();
  Check(true, "Narrow constraints must not produce invalid geometry");
}
} // namespace

int main() {
  try {
    DataTests();
    GeometryTests();
    ProjectionTests();
    ClippingTests();
    MarkerTests();
    LegendTests();
    HighlightGeometryTests();
    HighlightInteractionTests();
    RuntimeTests();
    ControlledTests();
    ScatterNavigationTests();
    std::cout << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "After " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
