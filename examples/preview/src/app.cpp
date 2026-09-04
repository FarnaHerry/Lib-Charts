#include <huxerui/huxerui.h>
#include <huxerui/charts.h>

#include <cmath>
#include <iomanip>
#include <sstream>

using namespace huxerui;

namespace {

XYChartData TrendData() {
  std::vector<XYDatum> first;
  std::vector<XYDatum> second;
  for (int i = 0; i < 2000; ++i) {
    const double x = i / 100.0;
    first.push_back({std::to_string(i), x, 5.0 + std::sin(x) * 2.0});
    second.push_back({std::to_string(i), x, 3.0 + std::cos(x * 0.7)});
  }
  return XYChartData({XYSeries("north", "North", std::move(first)), XYSeries("south", "South", std::move(second))});
}

CategoryChartData SalesData() {
  return CategoryChartData({{"jan", "January"}, {"feb", "February"}, {"mar", "March"}, {"apr", "April"}}, {
    CategorySeries("north", "North", {{"jan", "jan", 24}, {"feb", "feb", 36}, {"mar", "mar", 29}, {"apr", "apr", 45}}),
    CategorySeries("south", "South", {{"jan", "jan", 18}, {"feb", "feb", 21}, {"mar", "mar", 34}, {"apr", "apr", 27}}),
  });
}

ScatterChartData ScatterData() {
  std::vector<ScatterDatum> samples;
  for (int i = 0; i < 80; ++i) {
    samples.push_back({std::to_string(i), std::sin(i * 2.4) * 7 + i * 0.2,
                       std::cos(i * 1.8) * 5 + i * 0.1, static_cast<double>(i % 12)});
  }
  return ScatterChartData({ScatterSeries("observations", "Observations", std::move(samples))});
}

ThemeSpec GalleryTheme() {
  auto theme = MaterialLightThemeSpec();
  theme.colors.primary = Color::Rgb(55, 103, 218);
  theme.colors.secondary = Color::Rgb(30, 147, 138);
  theme.colors.secondary_container = Color::Rgb(232, 239, 255);
  theme.colors.on_secondary_container = Color::Rgb(40, 76, 160);
  theme.colors.background = Color::Rgb(245, 247, 250);
  theme.colors.surface = Color::White();
  theme.colors.surface_container_low = Color::White();
  theme.colors.surface_container = Color::Rgb(245, 247, 250);
  theme.colors.surface_container_high = Color::Rgb(238, 242, 247);
  theme.colors.surface_container_highest = Color::Rgb(232, 237, 244);
  theme.colors.on_surface = Color::Rgb(29, 42, 62);
  theme.colors.on_surface_variant = Color::Rgb(101, 116, 137);
  theme.colors.outline = Color::Rgb(210, 219, 231);
  return theme;
}

ChartTheme GalleryCharts(const ThemeSpec& theme) {
  ChartTheme charts;
  charts.palette = {theme.colors.primary, Color::Rgb(214, 146, 55), theme.colors.secondary, Color::Rgb(137, 105, 208)};
  charts.common.axis = {
      .line = theme.colors.outline,
      .grid = theme.colors.surface_container_highest,
      .text = TextStyle{Font::System(12.0F), theme.colors.on_surface_variant},
  };
  charts.common.legend.spacing = 20.0F;
  charts.line.stroke_style = StrokeStyle{.width = 2.5F, .cap = StrokeCap::Round, .join = StrokeJoin::Round};
  charts.line.marker.shape = ChartMarkerShape::Circle;
  charts.bar = {.stroke = Brush(Color::Transparent()), .corner_radius = 5.0F};
  charts.scatter.marker = {
      .shape = ChartMarkerShape::Circle,
      .fill = Brush(Color::Rgb(30, 147, 138, 0.55F)),
      .stroke = Brush(Color::White()),
      .stroke_style = StrokeStyle{.width = 1.5F},
  };
  return charts;
}

[[huxerui::composable]] View Caption(StringVariant text) {
  return Text(text).Style({Font::System(12.0F), UseTheme().colors.on_surface_variant});
}

[[huxerui::composable]] View CardHeading(StringVariant label, StringVariant title, StringVariant description) {
  const auto& theme = UseTheme();
  return Column {
    Text(label).Style({Font::System(11.0F).WithWeight(FontWeight::SemiBold), theme.colors.primary}),
    Text(title, TextRole::Title).Style({Font::System(20.0F).WithWeight(FontWeight::SemiBold), theme.colors.on_surface}),
    Caption(description),
  }.With(Spacing(6.0F), CrossAlign(CrossAxisAlignment::Stretch));
}

[[huxerui::composable]] View ChartCard(View content) {
  const auto& theme = UseTheme();
  return View(content).With(
      Padding(24.0F),
      Spacing(18.0F),
      CrossAlign(CrossAxisAlignment::Stretch),
      Background(theme.colors.surface),
      Border{.color = theme.colors.surface_container_highest},
      CornerRadius(16.0F)
  );
}

View CardPair(bool expanded, View first, View second) {
  if (expanded) {
    return Row {
      std::move(first).With(Grow()),
      std::move(second).With(Grow()),
    }.With(Spacing(20.0F), CrossAlign(CrossAxisAlignment::Stretch));
  }
  return Column {
    first,
    second,
  }.With(Spacing(20.0F), CrossAlign(CrossAxisAlignment::Stretch));
}

std::optional<ChartDatumIdentity> SelectedIdentity(const std::optional<ChartDatumInfo>& selection) {
  return selection ? std::optional(selection->identity) : std::nullopt;
}

View SelectionNote(const std::optional<ChartDatumInfo>& selection) {
  std::ostringstream text;
  text << std::fixed << std::setprecision(2);
  if (selection) {
    text << "Selected: " << selection->identity.series_key << "  /  ";
    if (const auto* xy = std::get_if<ChartXYValue>(&selection->value)) {
      text << "x " << xy->x << "  ·  y " << xy->y;
    } else if (const auto* category = std::get_if<ChartCategoryValue>(&selection->value)) {
      text << category->category_key << "  ·  " << category->value;
    } else if (const auto* scatter = std::get_if<ChartScatterValue>(&selection->value)) {
      text << "x " << scatter->x << "  ·  y " << scatter->y;
      if (scatter->magnitude) text << "  ·  size " << std::setprecision(0) << *scatter->magnitude;
    }
  } else {
    text << "Select a datum to inspect its values.";
  }
  return Caption(text.str());
}

[[huxerui::composable]] View Gallery() {
  static const auto initial_trend = TrendData();
  static const auto initial_sales = SalesData();
  static const auto initial_scatter = ScatterData();
  const auto trend = UseState(initial_trend);
  const auto sales = UseState(initial_sales);
  const auto scatter = UseState(initial_scatter);
  const auto interpolation = UseState(std::size_t{0});
  const auto bar_mode = UseState(std::size_t{0});
  const auto area_mode = UseState(std::size_t{0});
  const auto horizontal = UseState(false);
  // Keep controlled state above the responsive rows so resizing does not reset a chart's interaction.
  const auto line_selection = UseState(std::optional<ChartDatumInfo>{});
  const auto area_selection = UseState(std::optional<ChartDatumInfo>{});
  const auto bar_selection = UseState(std::optional<ChartDatumInfo>{});
  const auto category_selection = UseState(std::optional<ChartDatumInfo>{});
  const auto scatter_selection = UseState(std::optional<ChartDatumInfo>{});
  const bool expanded = UseViewportClass() == ViewportClass::Expanded;
  const auto& theme = UseTheme();
  constexpr ChartInterpolation interpolations[] = {
      ChartInterpolation::Linear, ChartInterpolation::Monotone,
      ChartInterpolation::StepBefore, ChartInterpolation::StepAfter,
  };
  constexpr AreaMode area_modes[] = {AreaMode::Unstacked, AreaMode::Stacked, AreaMode::PercentStacked};
  constexpr BarMode bar_modes[] = {BarMode::Grouped, BarMode::Stacked, BarMode::PercentStacked};
  auto trend_visibility = [trend](const ChartSeriesVisibilityRequest& request) {
    trend = trend->WithSeriesVisible(request.series_key, request.visible);
  };
  auto sales_visibility = [sales](const ChartSeriesVisibilityRequest& request) {
    sales = sales->WithSeriesVisible(request.series_key, request.visible);
  };
  View line = ChartCard(Column {
    Flow {
      CardHeading("LINE / NUMERIC", "Every point tells a story", "Two series · 4,000 samples · automatic sampling")
          .With(Grow()),
      SegmentedButton({"Linear", "Monotone", "Step before", "Step after"}, interpolation)
          .OnChanged([interpolation](std::size_t index) { interpolation = index; })
          .With(Semantics{.label = "Line interpolation"}),
    }.With(Spacing(16.0F), CrossAlign(CrossAxisAlignment::Center)),
    LineChart(trend.Get())
        .Interpolation(interpolations[interpolation.Get()])
        .XAxis(ChartAxis::Linear().Title("Time").Grid(false))
        .YAxis(ChartAxis::Linear().Title("Value").IncludeZero().TickCount(5))
        .Accessibility({.summary = "Numeric line chart with 4,000 samples"})
        .Selection(SelectedIdentity(line_selection.Get()))
        .On<ChartEvents::SelectionRequested>([line_selection](const ChartDatumInfo& info) { line_selection = info; })
        .On<ChartEvents::SeriesVisibilityRequested>(trend_visibility)
        .With(Frame{.height = 280.0F}),
    SelectionNote(line_selection.Get()),
  });
  View area = ChartCard(Column {
    CardHeading("AREA / COMPOSITION", "See the bigger picture", "The trend data, viewed as a whole or as a share."),
    Flow {
      SegmentedButton({"Overlay", "Stacked", "100%"}, area_mode)
          .OnChanged([area_mode](std::size_t index) { area_mode = index; })
          .With(Semantics{.label = "Area mode"}),
    },
    AreaChart(trend.Get())
        .Mode(area_modes[area_mode.Get()])
        .XAxis(ChartAxis::Linear().Title("Time").TickCount(5).Grid(false))
        .YAxis(ChartAxis::Linear().Title(area_mode.Get() == 2 ? "Share" : "Value").TickCount(5))
        .SeriesStyle("north", {.fill = Brush(Color::Rgb(55, 103, 218, 0.16F))})
        .SeriesStyle("south", {.fill = Brush(Color::Rgb(30, 147, 138, 0.22F))})
        .Animation({.enabled = true})
        .Accessibility({.summary = "Area chart sharing the numeric line data"})
        .Selection(SelectedIdentity(area_selection.Get()))
        .On<ChartEvents::SelectionRequested>([area_selection](const ChartDatumInfo& info) { area_selection = info; })
        .On<ChartEvents::SeriesVisibilityRequested>(trend_visibility)
        .With(Frame{.height = 260.0F}),
    SelectionNote(area_selection.Get()),
    Caption("Legend visibility is shared with the numeric line chart."),
  });
  View bars = ChartCard(Column {
    CardHeading("BAR / COMPARISON", "A little perspective", "Compare North and South across four months."),
    Flow {
      SegmentedButton({"Grouped", "Stacked", "100%"}, bar_mode)
          .OnChanged([bar_mode](std::size_t index) { bar_mode = index; })
          .With(Semantics{.label = "Bar mode"}),
      Switch("Horizontal", horizontal).OnChanged([horizontal](bool value) { horizontal = value; }),
    }.With(Spacing(16.0F), CrossAlign(CrossAxisAlignment::Center)),
    BarChart(sales.Get())
        .Mode(bar_modes[bar_mode.Get()])
        .Orientation(horizontal.Get() ? ChartOrientation::Horizontal : ChartOrientation::Vertical)
        .CategoryAxis(ChartAxis::Category().Grid(false))
        .ValueAxis(ChartAxis::Linear().Title(bar_mode.Get() == 2 ? "Share" : "Sales").IncludeZero().TickCount(5))
        .Animation({.enabled = true})
        .Accessibility({.summary = "Monthly sales bar chart"})
        .Selection(SelectedIdentity(bar_selection.Get()))
        .On<ChartEvents::SelectionRequested>([bar_selection](const ChartDatumInfo& info) { bar_selection = info; })
        .On<ChartEvents::SeriesVisibilityRequested>(sales_visibility)
        .With(Frame{.height = 260.0F}),
    SelectionNote(bar_selection.Get()),
    Caption("Legend visibility is shared with the category line chart."),
  });
  View categories = ChartCard(Column {
    CardHeading("LINE / CATEGORIES", "Same data, another angle", "Monthly sales, connected in category order."),
    LineChart(sales.Get())
        .XAxis(ChartAxis::Category().Grid(false))
        .YAxis(ChartAxis::Linear().Title("Sales").IncludeZero().TickCount(5))
        .Style({.marker = {.visible = true, .area = 48.0F, .fill = Brush(Color::White()),
                           .stroke_style = StrokeStyle{.width = 2.0F}}})
        .Accessibility({.summary = "Monthly sales category line chart"})
        .Selection(SelectedIdentity(category_selection.Get()))
        .On<ChartEvents::SelectionRequested>([category_selection](const ChartDatumInfo& info) {
          category_selection = info;
        })
        .On<ChartEvents::SeriesVisibilityRequested>(sales_visibility)
        .With(Frame{.height = 300.0F}),
    SelectionNote(category_selection.Get()),
    Caption("Point markers keep each category easy to inspect."),
  });
  View bubbles = ChartCard(Column {
    CardHeading("SCATTER / MAGNITUDE", "Room for discovery", "80 observations · bubble area represents magnitude"),
    ScatterChart(scatter.Get())
        .XAxis(ChartAxis::Linear().Title("X").TickCount(5))
        .YAxis(ChartAxis::Linear().Title("Y").TickCount(5))
        .SizeScale({.min_area = 28.0F, .max_area = 280.0F})
        .Accessibility({.summary = "Bubble scatter chart with 80 observations"})
        .Selection(SelectedIdentity(scatter_selection.Get()))
        .On<ChartEvents::SelectionRequested>([scatter_selection](const ChartDatumInfo& info) {
          scatter_selection = info;
        })
        .On<ChartEvents::SeriesVisibilityRequested>([scatter](const ChartSeriesVisibilityRequest& request) {
          scatter = scatter->WithSeriesVisible(request.series_key, request.visible);
        })
        .With(Frame{.height = 300.0F}),
    SelectionNote(scatter_selection.Get()),
    Caption("Translucent fills reveal overlapping observations."),
  });
  return ScrollView(Column {
    Column {
      Text("HuxerUI Charts", TextRole::Title)
          .Style({Font::System(32.0F).WithWeight(FontWeight::SemiBold), theme.colors.on_surface}),
      Text("A gallery of data, detail, and interaction.")
          .Style({Font::System(15.0F), theme.colors.on_surface_variant}),
      Caption("Hover for details · click to select · use arrow keys to explore · toggle a legend to compare"),
    }.With(Spacing(10.0F), CrossAlign(CrossAxisAlignment::Stretch)),
    line,
    CardPair(expanded, area, bars),
    CardPair(expanded, categories, bubbles),
    Caption("Built with HuxerUI Charts · sample data for demonstration"),
  }.With(Padding(expanded ? 32.0F : 20.0F), Spacing(24.0F), CrossAlign(CrossAxisAlignment::Stretch)))
      .With(Background(theme.colors.background), ScrollBar());
}

} // namespace

View App() {
  static const auto theme = GalleryTheme();
  static const auto charts = GalleryCharts(theme);
  return MaterialTheme(theme, ProvideEnvironment(charts, Gallery()));
}

const Application application{
    App,
    {
        .window = {
            .title = "Charts Preview",
            .initial_size = {1360.0F, 900.0F},
            .minimum_size = Size{960.0F, 640.0F},
        },
        .viewport_breakpoints = {.medium_width = 600.0F, .expanded_width = 1120.0F},
        .show_debug_overlay = false,
    }
};
