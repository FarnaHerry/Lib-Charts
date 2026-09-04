# HuxerUI Charts

Charts for HuxerUI applications, with tooltips, interactive legends, selection, keyboard navigation and accessibility support. Customize axes, colors and styles to match your app, with support for localized labels and animated data updates.

## Supported charts

| Component | Features |
| --- | --- |
| `LineChart` | Straight, smooth and step lines, with optional point markers |
| `AreaChart` | Filled areas, stacked areas and percentage stacks |
| `BarChart` | Vertical or horizontal bars, grouped or stacked |
| `ScatterChart` | Scatter plots and bubble charts |

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

## Examples and documentation

- [Example gallery](examples/preview/src/app.cpp): all four chart types, stacking, selection, interactive legends and animations.
- [API reference](include/huxerui/charts.h): detailed usage, data requirements, configuration options and examples.

## License

[MIT](LICENSE).
