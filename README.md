# HuxerUI Charts

Charts for HuxerUI applications, with tooltips, interactive legends, selection, keyboard navigation and accessibility support. Customize axes, colors and styles to match your app, with support for localized labels and animated data updates.

## Supported charts

| Component | Features |
| --- | --- |
| `LineChart` | Straight, smooth and step lines, with optional point markers |
| `AreaChart` | Filled areas, stacked areas and percentage stacks |
| `BarChart` | Vertical or horizontal bars, grouped or stacked |
| `ScatterChart` | Scatter plots and bubble charts |
| `PieChart` | Solid pies, donuts and rounded donuts with animated hover feedback |

## Installation

Use an installed HuxerUI SDK and a C++20-capable compiler. In a HuxerUI CLI-created app, add this after your application target in `CMakeLists.txt`:

```cmake
huxerui_use_library(my_app
    TARGET HuxerUI::Charts
    URL "https://github.com/HuxerUI/Lib-Charts.git"
    TAG "v0.1.0"
)
```

Replace `my_app` with the target name in your existing `huxerui_add_app(...)` call. No separate library checkout or initialization is needed.

## Quick start

Add this component to a `.cpp` file in your app's `src` directory:

```cpp
#include <huxerui/huxerui.h>
#include <huxerui/charts.h>

using namespace huxerui;

View SalesChart() {
  static const CategoryChartData data({
    {"jan", "January"},
    {"feb", "February"},
    {"mar", "March"},
  }, {
    CategorySeries("sales", "Sales", {
      {"jan", "jan", 24},
      {"feb", "feb", 36},
      {"mar", "mar", 29},
    }),
  });

  return BarChart(data)
      .Title("Monthly sales")
      .Legend({.visible = false})
      .With(Frame{.height = 280.0F});
}
```

Call `SalesChart()` from your app's UI inside a Flat or Material theme. Set the height with `Frame` and let the parent layout provide the width.

For a rounded donut, supply nonnegative values with stable keys:

```cpp
View TrafficMix() {
  static const PieChartData data({
    {"upload", "Upload", 72.0},
    {"download", "Download", 28.0},
  });
  return PieChart(data)
      .Title("Traffic mix")
      .Style({.inner_radius = 0.62F, .rounded_caps = true})
      .SliceStyle("upload", {.fill = Brush(Color::Rgb(230, 184, 102))})
      .SliceStyle("download", {.fill = Brush(Color::Rgb(72, 118, 232))})
      .With(Frame{.width = 200.0F, .height = 240.0F});
}
```

`PieChart` renders a solid pie by default. Set `inner_radius` for a donut, and
`rounded_caps` for rounded ends, a center percentage and a 180 ms hover transition
that honors reduced motion. Geometry is configured for the whole chart; `SliceStyle`
customizes paint. Place any legend or additional labels alongside the chart as ordinary Views.

An empty snapshot keeps a neutral shape with the configured hole. All-zero values
use equal placeholder slices while their displayed and accessible percentages remain
zero. With a positive total, zero-valued entries occupy no space. Invalid keys,
negative or nonfinite values, and invalid styles are rejected.

## Examples and documentation

- [Example gallery](examples/preview/src/app.cpp): Cartesian and pie charts, stacking, selection, interactive legends and animations.
- [API reference](include/huxerui/charts.h): detailed usage, data requirements, configuration options and examples.

## License

[MIT](LICENSE).
