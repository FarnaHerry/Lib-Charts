#include "chart_internal.h"

namespace huxerui::charts_detail {

std::shared_ptr<const Config> Initial(Kind kind, std::shared_ptr<const Data> data, SeriesStyle style) {
  auto config = std::make_shared<Config>();
  config->kind = kind;
  config->data = std::move(data);
  config->style = std::move(style);
  if (config->data->kind == DataKind::Category) config->x = ChartAxis::Category();
  if (kind == Kind::Bar || kind == Kind::Area) config->y = ChartAxis::Linear().IncludeZero();
  return config;
}

template <class D> ChartComponent<D>::ChartComponent(std::shared_ptr<const Config> config)
    : View(BuildChart(config)), config_(std::move(config)) {}

template <class D> template <class F> D ChartComponent<D>::Change(F&& function) {
  // Fluent declarations may share their configuration with earlier View copies; never mutate that snapshot.
  auto config = std::make_shared<Config>(*config_);
  function(*config);
  config_ = config;
  // Codegen makes BuildChart a deferred Scope. Replacing the declaration does not eagerly run composition hooks.
  View::operator=(BuildChart(config));
  return std::move(static_cast<D&>(*this));
}

template <class D> D ChartComponent<D>::Title(StringVariant value) && {
  return Change([&](Config& c) { c.title = std::move(value); });
}
template <class D> D ChartComponent<D>::Selection(std::optional<ChartDatumIdentity> value) && {
  return Change([&](Config& c) { c.selection = std::move(value); });
}
template <class D> D ChartComponent<D>::Appearance(ChartStyle value) && {
  return Change([&](Config& c) { c.appearance = std::move(value); });
}
template <class D> D ChartComponent<D>::Legend(ChartLegendOptions value) && {
  return Change([&](Config& c) { c.legend = value; });
}
template <class D> D ChartComponent<D>::Tooltip(ChartTooltipOptions value) && {
  return Change([&](Config& c) { c.tooltip = std::make_shared<const ChartTooltipOptions>(std::move(value)); });
}
template <class D> D ChartComponent<D>::Animation(ChartAnimationOptions value) && {
  return Change([&](Config& c) { c.animation = value; });
}
template <class D> D ChartComponent<D>::Accessibility(ChartAccessibilityOptions value) && {
  return Change([&](Config& c) { c.accessibility = std::move(value); });
}

template class ChartComponent<LineChart>;
template class ChartComponent<AreaChart>;
template class ChartComponent<BarChart>;
template class ChartComponent<ScatterChart>;

} // namespace huxerui::charts_detail

namespace huxerui {
using charts_detail::Config;
using charts_detail::Initial;
using charts_detail::Kind;
using charts_detail::Access;

LineChart::LineChart(XYChartData data) : ChartComponent(Initial(Kind::Line, Access::DataOf(data), LineChartStyle{})) {}
LineChart::LineChart(CategoryChartData data) : ChartComponent(Initial(Kind::Line, Access::DataOf(data), LineChartStyle{})) {}
AreaChart::AreaChart(XYChartData data) : ChartComponent(Initial(Kind::Area, Access::DataOf(data), AreaChartStyle{})) {}
AreaChart::AreaChart(CategoryChartData data) : ChartComponent(Initial(Kind::Area, Access::DataOf(data), AreaChartStyle{})) {}
BarChart::BarChart(CategoryChartData data) : ChartComponent(Initial(Kind::Bar, Access::DataOf(data), BarChartStyle{})) {}
ScatterChart::ScatterChart(ScatterChartData data) : ChartComponent(Initial(Kind::Scatter, Access::DataOf(data), ScatterChartStyle{})) {}

LineChart LineChart::XAxis(ChartAxis value) && { return Change([&](Config& c) { c.x = std::move(value); }); }
LineChart LineChart::YAxis(ChartAxis value) && { return Change([&](Config& c) { c.y = std::move(value); }); }
LineChart LineChart::Interpolation(ChartInterpolation value) && { return Change([&](Config& c) { c.interpolation = value; }); }
LineChart LineChart::Sampling(ChartSampling value) && { return Change([&](Config& c) { c.sampling = value; }); }
LineChart LineChart::Style(LineChartStyle value) && { return Change([&](Config& c) { c.style = std::move(value); }); }
LineChart LineChart::SeriesStyle(std::string key, LineChartStyle value) && {
  return Change([&](Config& c) { c.series_styles[std::move(key)] = std::move(value); });
}
AreaChart AreaChart::XAxis(ChartAxis value) && { return Change([&](Config& c) { c.x = std::move(value); }); }
AreaChart AreaChart::YAxis(ChartAxis value) && { return Change([&](Config& c) { c.y = std::move(value); }); }
AreaChart AreaChart::Mode(AreaMode value) && { return Change([&](Config& c) { c.area_mode = value; }); }
AreaChart AreaChart::Baseline(double value) && { return Change([&](Config& c) { c.baseline = value; }); }
AreaChart AreaChart::Interpolation(ChartInterpolation value) && { return Change([&](Config& c) { c.interpolation = value; }); }
AreaChart AreaChart::Sampling(ChartSampling value) && { return Change([&](Config& c) { c.sampling = value; }); }
AreaChart AreaChart::Style(AreaChartStyle value) && { return Change([&](Config& c) { c.style = std::move(value); }); }
AreaChart AreaChart::SeriesStyle(std::string key, AreaChartStyle value) && {
  return Change([&](Config& c) { c.series_styles[std::move(key)] = std::move(value); });
}
BarChart BarChart::CategoryAxis(ChartAxis value) && { return Change([&](Config& c) { c.x = std::move(value); }); }
BarChart BarChart::ValueAxis(ChartAxis value) && { return Change([&](Config& c) { c.y = std::move(value); }); }
BarChart BarChart::Mode(BarMode value) && { return Change([&](Config& c) { c.bar_mode = value; }); }
BarChart BarChart::Orientation(ChartOrientation value) && { return Change([&](Config& c) { c.orientation = value; }); }
BarChart BarChart::Style(BarChartStyle value) && { return Change([&](Config& c) { c.style = std::move(value); }); }
BarChart BarChart::SeriesStyle(std::string key, BarChartStyle value) && {
  return Change([&](Config& c) { c.series_styles[std::move(key)] = std::move(value); });
}
ScatterChart ScatterChart::XAxis(ChartAxis value) && { return Change([&](Config& c) { c.x = std::move(value); }); }
ScatterChart ScatterChart::YAxis(ChartAxis value) && { return Change([&](Config& c) { c.y = std::move(value); }); }
ScatterChart ScatterChart::SizeScale(ScatterSizeScale value) && { return Change([&](Config& c) { c.size_scale = value; }); }
ScatterChart ScatterChart::Sampling(ScatterSampling value) && { return Change([&](Config& c) { c.scatter_sampling = value; }); }
ScatterChart ScatterChart::Style(ScatterChartStyle value) && { return Change([&](Config& c) { c.style = std::move(value); }); }
ScatterChart ScatterChart::SeriesStyle(std::string key, ScatterChartStyle value) && {
  return Change([&](Config& c) { c.series_styles[std::move(key)] = std::move(value); });
}

PieChart::PieChart(PieChartData data) {
  auto config = std::make_shared<charts_detail::PieConfig>();
  config->data = std::move(data);
  config_ = std::move(config);
  View::operator=(charts_detail::BuildPieChart(config_));
}

PieChart PieChart::Change(std::function<void(charts_detail::PieConfig&)>&& update) {
  auto config = std::make_shared<charts_detail::PieConfig>(*config_);
  update(*config);
  config_ = config;
  View::operator=(charts_detail::BuildPieChart(config));
  return std::move(*this);
}

PieChart PieChart::Title(StringVariant value) && {
  return Change([&](charts_detail::PieConfig& config) { config.title = std::move(value); });
}

PieChart PieChart::Appearance(ChartStyle value) && {
  return Change([&](charts_detail::PieConfig& config) { config.appearance = std::move(value); });
}

PieChart PieChart::Accessibility(ChartAccessibilityOptions value) && {
  return Change([&](charts_detail::PieConfig& config) { config.accessibility = std::move(value); });
}

PieChart PieChart::Style(PieChartStyle value) && {
  return Change([&](charts_detail::PieConfig& config) { config.style = std::move(value); });
}

PieChart PieChart::SliceStyle(std::string key, PieChartStyle value) && {
  return Change([&](charts_detail::PieConfig& config) {
    config.slice_styles[std::move(key)] = std::move(value);
  });
}

} // namespace huxerui
