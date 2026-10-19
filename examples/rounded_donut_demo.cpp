#include <huxerui/charts.h>
#include <huxerui/huxerui.h>

using namespace huxerui;

namespace {

ThemeSpec DemoTheme() {
  auto theme = MaterialLightThemeSpec();
  theme.colors.primary = Color::Rgb(49, 100, 216);
  theme.colors.background = Color::Rgb(245, 247, 250);
  theme.colors.surface = Color::White();
  theme.colors.on_surface = Color::Rgb(30, 42, 62);
  theme.colors.on_surface_variant = Color::Rgb(101, 116, 137);
  return theme;
}

ChartTheme DemoCharts() {
  ChartTheme charts;
  charts.palette = {Color::Rgb(49, 100, 216), Color::Rgb(224, 151, 54)};
  return charts;
}

} // namespace

View App() {
  static const auto theme = DemoTheme();
  static const auto charts = DemoCharts();
  static const PieChartData traffic({
      {"upload", "Upload", 72.0},
      {"download", "Download", 28.0},
  });

  return MaterialTheme(theme, ProvideEnvironment(charts, Column {
    Text("Rounded donut", TextRole::Title)
        .Style({Font::System(30.0F).WithWeight(FontWeight::SemiBold), theme.colors.on_surface}),
    Text("Hover an arc to highlight it and match the center color.")
        .Style({Font::System(15.0F), theme.colors.on_surface_variant}),
    PieChart(traffic)
        .Style({.inner_radius = 0.62F, .rounded_caps = true})
        .SliceStyle("upload", {.fill = Brush(Color::Rgb(49, 100, 216))})
        .SliceStyle("download", {.fill = Brush(Color::Rgb(224, 151, 54))})
        .With(Frame{.width = 320.0F, .height = 320.0F}),
    Row {
      Text("Upload: 72%").Style({Font::System(14.0F), Color::Rgb(49, 100, 216)}),
      Text("Download: 28%").Style({Font::System(14.0F), Color::Rgb(224, 151, 54)}),
    }.With(Spacing(28.0F), MainAlign(MainAxisAlignment::Center)),
  }.With(Padding(40.0F), Spacing(20.0F), CrossAlign(CrossAxisAlignment::Center),
         MainAlign(MainAxisAlignment::Center), Background(theme.colors.background))));
}

const Application application{
    App,
    {
        .window = {
            .title = "Rounded Donut Demo",
            .initial_size = {720.0F, 720.0F},
            .minimum_size = Size{560.0F, 560.0F},
        },
        .show_debug_overlay = false,
    }
};
