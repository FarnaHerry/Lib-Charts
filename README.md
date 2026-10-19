# HuxerUI Charts

This downstream fork maintains application fixes on `farna/main`, including pie charts and plot spacing for compact layouts. The official baseline is tracked through `upstream/main` from `HuxerUI/Lib-Charts`. Consumers should pin a validated commit.

Charts for HuxerUI applications, with tooltips, interactive legends, selection, keyboard navigation and accessibility support. Customize axes, colors and styles to match your app, with support for localized labels and animated data updates.

## Supported charts

| Component | Features |
| --- | --- |
| `LineChart` | Straight, smooth and step lines, with optional point markers |
| `AreaChart` | Filled areas, stacked areas and percentage stacks |
| `BarChart` | Vertical or horizontal bars, grouped or stacked |
| `ScatterChart` | Scatter plots and bubble charts |
| `PieChart` | Proportional slices for part-to-whole comparisons |

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

View TrafficMix() {
  static const PieChartData data({
    {"upload", "Upload", 72.0},
    {"download", "Download", 28.0},
  });

  return PieChart(data)
      .Style({.inner_radius = 0.62F, .rounded_caps = true})
      .Title("Traffic mix")
      .SliceStyle("upload", {.fill = Brush(Color::Rgb(230, 184, 102))})
      .SliceStyle("download", {.fill = Brush(Color::Rgb(72, 118, 232))})
      .With(Frame{.width = 96.0F, .height = 96.0F});
}
```

`rounded_caps` opts into a rounded donut with the largest share in its center. Hover a
segment to expand it with a soft halo and show its percentage in the same color.
Expansion, halo opacity and text color ease smoothly over 180 ms, including interrupted hover changes;
leaving the segment restores the largest share and the default text color. Omit
the option to keep the existing solid pie or ordinary donut. A rounded donut uses
`inner_radius = 0.62` if no hole radius is supplied.

Call these composables from your app's UI inside a Flat or Material theme. Set the chart dimensions with `Frame`.

## Examples and documentation

- [Example gallery](examples/preview/src/app.cpp): Cartesian and pie charts, stacking, selection, interactive legends and animations.
- [API reference](include/huxerui/charts.h): detailed usage, data requirements, configuration options and examples.

To build the focused rounded donut demo on Windows, configure the project with `HUXERUI_CHARTS_BUILD_PREVIEW=ON`, then build and run `rounded_donut_demo`:

```powershell
cmake -S . -B build -DHUXERUI_HOME="C:/path/to/HuxerUI" -DHUXERUI_CHARTS_BUILD_PREVIEW=ON
cmake --build build --config Debug --target rounded_donut_demo
& .\build\examples\preview\Debug\rounded_donut_demo.exe
```

## License

[MIT](LICENSE).
