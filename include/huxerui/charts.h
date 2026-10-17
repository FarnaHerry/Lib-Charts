#pragma once

/// @file
/// @brief Public data, styling, events and View declarations for HuxerUI charts.
///
/// Link the HuxerUI::Charts target through huxerui_use_library(). No root installation hook is required.
/// Chart data is immutable and shared; selection and series visibility remain application-controlled.
/// Configure chart-specific fluent methods before View::On(), View::With() or View::Key(), which return View.
/// All geometry and style lengths use device-independent pixels (DIPs), except explicitly documented areas.
/// Resource-backed text accepts generated resource identifiers through StringVariant. Include the resource
/// compiler's generated header at the use site instead of constructing StringResource identifiers manually.
/// Examples assume <huxerui/huxerui.h>, <huxerui/charts.h> and using namespace huxerui. Place examples using
/// UseState() inside a composable function defined in a codegen-enabled implementation file.

#include <huxerui/animation.h>
#include <huxerui/paint.h>
#include <huxerui/view.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace huxerui {

/// Selects how successive nonmissing samples form a line or an area's upper boundary.
/// Missing ordinates and absent categories split contours regardless of interpolation.
enum class ChartInterpolation {
  /// Connects successive samples with straight segments; the default interpolation.
  Linear,
  /// Produces a shape-preserving smooth curve; unavailable for stacked areas.
  Monotone,
  /// Changes to the next ordinate at the previous sample's x, then advances horizontally.
  StepBefore,
  /// Holds the previous ordinate until the next sample's x, then changes vertically.
  StepAfter
};

/// Selects the baseline and accumulation policy for an AreaChart.
enum class AreaMode {
  /// Fills each series independently to the configured baseline; the default mode.
  Unstacked,
  /// Accumulates visible positive and negative values separately from zero.
  Stacked,
  /// Normalizes nonnegative visible values at each x to a total of one; zero totals form gaps.
  PercentStacked
};

/// Selects the arrangement of visible series within each BarChart category.
enum class BarMode {
  /// Places series side by side; a missing datum leaves its series slot empty. This is the default mode.
  Grouped,
  /// Accumulates positive and negative segments separately; missing segments are omitted.
  Stacked,
  /// Normalizes nonnegative visible segments to a total of one; zero totals produce no segments.
  PercentStacked
};

/// Selects the value direction of a BarChart without changing its category/value axis APIs.
enum class ChartOrientation {
  /// Draws columns along a vertical value axis with categories ordered left to right; the default.
  Vertical,
  /// Draws bars along a horizontal value axis with categories ordered top to bottom.
  Horizontal
};

/// Selects a data marker's filled shape. Marker visibility is configured separately.
enum class ChartMarkerShape {
  /// A circle.
  Circle,
  /// An axis-aligned square.
  Square,
  /// A square rotated by 45 degrees.
  Diamond,
  /// An upward-pointing equilateral triangle.
  Triangle,
  /// A filled plus-shaped cross with equal-width arms.
  Cross
};

/// Places the legend outside the plotting surface.
/// Swatches use the resolved series brushes, stroke style and marker shape, including keyed overrides.
enum class ChartLegendPlacement {
  /// Places a wrapping legend above the plotting surface.
  Top,
  /// Places a wrapping legend below the plotting surface; the default.
  Bottom,
  /// Places the legend before the plotting surface in a horizontal row.
  Start,
  /// Places the legend after the plotting surface in a horizontal row.
  End
};

/// Controls presentation-only simplification for numeric lines and areas.
/// Category data is not automatically aggregated or decimated by this setting.
enum class ChartSampling {
  /// Retains all samples in the visible x interval and the required boundary neighbors.
  None,
  /// Preserves bucket endpoints, extrema and gaps at approximately pixel density; the default.
  /// Numeric area stacks share retained x positions. Many explicit gaps can exceed the usual density budget.
  Auto
};

/// Controls presentation-only reduction of scatter data without changing the source snapshot.
enum class ScatterSampling {
  /// Displays every visible scatter datum; the default.
  Exact,
  /// Displays representatives from nearby screen-space cells, retaining selected/cursor data as needed.
  /// Accessibility summaries identify this mode; representative values are not aggregate values.
  Representative
};

/// Defines one category in the authoritative order stored by CategoryChartData.
struct ChartCategory {
  /// Nonempty stable identifier, unique across the snapshot's categories.
  std::string key;
  /// Display text or a generated localized string resource; independent of the category key.
  StringVariant label;
  bool operator==(const ChartCategory&) const = default;
};
/// Stores one numeric sample for an XYSeries.
/// Values must be finite, and x values must be strictly increasing within a series.
struct XYDatum {
  /// Nonempty stable datum identifier, unique within its series.
  std::string key;
  /// Numeric horizontal coordinate; must remain finite even when y is missing.
  double x = 0.0;
  /// Optional finite ordinate. std::nullopt breaks the contour; it does not mean zero.
  std::optional<double> y;
  bool operator==(const XYDatum&) const = default;
};
/// Associates one finite value with a category in a CategoryChartData snapshot.
/// Missing categories are represented by absent data, not an artificial zero-valued datum.
struct CategoryDatum {
  /// Nonempty stable datum identifier, unique within its series.
  std::string key;
  /// Key of an existing ChartCategory; at most one datum per series may reference that category.
  std::string category_key;
  /// Original finite business value. Percent-stacked charts additionally require nonnegative values.
  double value = 0.0;
  bool operator==(const CategoryDatum&) const = default;
};
/// Stores one unordered numeric sample and an optional bubble magnitude for a ScatterSeries.
struct ScatterDatum {
  /// Nonempty stable datum identifier, unique within its series.
  std::string key;
  /// Finite horizontal coordinate; unlike XYDatum, scatter coordinates need not be sorted or unique.
  double x = 0.0;
  /// Finite vertical coordinate. Scatter data does not use missing ordinates to represent gaps.
  double y = 0.0;
  /// Optional finite, nonnegative magnitude mapped to marker area by ScatterSizeScale.
  /// std::nullopt uses ChartMarkerStyle::area instead of magnitude-based sizing.
  std::optional<double> magnitude;
  bool operator==(const ScatterDatum&) const = default;
};

namespace charts_detail {
/// @cond HUXERUI_CHARTS_INTERNAL
struct Data;
struct Config;
struct Access;
struct PieConfig;
/// @endcond

/// Owns immutable samples together with a stable series key, display label and visibility value.
///
/// Copies share the sample allocation, while metadata remains a value. Use XYSeries, CategorySeries or
/// ScatterSeries in application code. Data validation occurs when the series is placed in its chart snapshot.
/// @tparam Datum The sample type: XYDatum, CategoryDatum or ScatterDatum.
template <class Datum> class Series {
public:
  Series(std::string key, StringVariant label, std::vector<Datum> samples, bool visible = true)
      : key_(std::move(key)), label_(std::move(label)),
        samples_(std::make_shared<const std::vector<Datum>>(std::move(samples))), visible_(visible) {}
  /// Returns the series identifier used by selection, visibility requests and keyed styles.
  /// @return A borrowed key reference, valid while this series object is unchanged and alive.
  [[nodiscard]] const std::string& Key() const noexcept { return key_; }
  /// Returns the declarative display label without resolving localized resources.
  /// @return A borrowed label reference, valid while this series object is unchanged and alive.
  [[nodiscard]] const StringVariant& Label() const noexcept { return label_; }
  /// Borrows immutable samples in their original input order.
  /// @return A span valid while at least one series or snapshot retains this sample allocation.
  [[nodiscard]] std::span<const Datum> Samples() const noexcept { return *samples_; }
  /// Reports whether the series participates in plotting, domains and stacking.
  /// @return The stored visibility value; false does not remove the series from the legend or snapshot.
  [[nodiscard]] bool Visible() const noexcept { return visible_; }
  /// Returns a series value with updated visibility without modifying this series or copying samples.
  /// @param visible Whether the returned series participates in the chart's visible data.
  /// @return A series sharing the same immutable samples, key and label with the requested visibility.
  [[nodiscard]] Series WithVisible(bool visible) const {
    auto result = *this;
    result.visible_ = visible;
    return result;
  }
  bool operator==(const Series&) const = default;
private:
  std::string key_;
  StringVariant label_;
  std::shared_ptr<const std::vector<Datum>> samples_;
  bool visible_;
};
} // namespace charts_detail

/// A named, ordered numeric series shared by LineChart and AreaChart.
/// @see XYDatum, XYChartData
using XYSeries = charts_detail::Series<XYDatum>;
/// A named category/value series shared by LineChart, AreaChart and BarChart.
/// Sample input order is retained, while CategoryChartData determines plotting order.
/// @see CategoryDatum, CategoryChartData
using CategorySeries = charts_detail::Series<CategoryDatum>;
/// A named unordered numeric series used by ScatterChart for scatter or bubble data.
/// @see ScatterDatum, ScatterChartData
using ScatterSeries = charts_detail::Series<ScatterDatum>;

/// Holds an immutable, indexed snapshot of numeric series for lines and areas.
///
/// Snapshot validation rejects empty/duplicate keys, nonfinite coordinates and non-increasing x values with
/// std::invalid_argument. Initial validation/indexing is linear in the number of samples; subsequent copies share
/// that storage. Retain large snapshots instead of rebuilding their sample vectors on every composition.
/// @code
/// XYChartData data({XYSeries("temperature", "Temperature", {
///   {"morning", 8.0, 12.0},
///   {"noon", 12.0, std::nullopt},
///   {"evening", 18.0, 16.0},
/// })});
/// auto hidden = data.WithSeriesVisible("temperature", false);
/// @endcode
class XYChartData {
public:
  explicit XYChartData(std::vector<XYSeries> series = {});
  /// Borrows the series in their declaration and stacking order, including hidden series.
  /// @return An immutable span valid while this snapshot's shared storage remains alive.
  [[nodiscard]] std::span<const XYSeries> Series() const noexcept;
  /// Changes one series' visibility in a new snapshot, sharing all sample storage and indices.
  /// @param key An existing stable series key, not a display label or vector index.
  /// @param visible Whether the series contributes to plotting, domains and stacking.
  /// @return The unchanged snapshot when visibility already matches, otherwise an updated snapshot value.
  /// @throws std::invalid_argument if key does not identify a series in this snapshot.
  [[nodiscard]] XYChartData WithSeriesVisible(std::string_view key, bool visible) const;
  bool operator==(const XYChartData&) const = default;
private:
  std::shared_ptr<const charts_detail::Data> data_;
  friend struct charts_detail::Access;
};
/// Holds an immutable category table and series for category lines, areas and bars.
///
/// Categories are the single authoritative plotting order. Snapshot validation rejects invalid/duplicate keys,
/// unknown categories, multiple data for one series/category and nonfinite values with std::invalid_argument.
/// Category lookup indices may reorder references for plotting, but never reorder the original sample storage.
/// @code
/// CategoryChartData data({{"jan", "January"}, {"feb", "February"}}, {
///   CategorySeries("revenue", "Revenue", {{"jan-value", "jan", 24.0}, {"feb-value", "feb", 36.0}}),
///   CategorySeries("cost", "Cost", {{"jan-cost", "jan", 18.0}}),
/// });
/// @endcode
class CategoryChartData {
public:
  explicit CategoryChartData(std::vector<ChartCategory> categories = {}, std::vector<CategorySeries> series = {});
  /// Borrows the authoritative category table, including categories with no present data.
  /// @return An immutable span in plotting order, valid while its shared category allocation remains alive.
  [[nodiscard]] std::span<const ChartCategory> Categories() const noexcept;
  /// Borrows series in declaration order without reordering their original sample arrays.
  /// @return An immutable span, including hidden series, valid while the snapshot's shared storage remains alive.
  [[nodiscard]] std::span<const CategorySeries> Series() const noexcept;
  /// Changes one series' visibility without copying samples, rebuilding indices or changing category order.
  /// @param key An existing stable series key.
  /// @param visible Whether the series contributes to plotting, domains and stacking.
  /// @return The unchanged snapshot when visibility already matches, otherwise an updated snapshot value.
  /// @throws std::invalid_argument if key does not identify a series in this snapshot.
  [[nodiscard]] CategoryChartData WithSeriesVisible(std::string_view key, bool visible) const;
  bool operator==(const CategoryChartData&) const = default;
private:
  std::shared_ptr<const charts_detail::Data> data_;
  friend struct charts_detail::Access;
};
/// Holds an immutable, indexed snapshot of scatter or bubble series.
///
/// Sample order is preserved, and duplicate coordinates are allowed. Snapshot validation rejects empty/duplicate
/// keys, nonfinite coordinates and negative/nonfinite magnitudes with std::invalid_argument.
/// @code
/// ScatterChartData data({ScatterSeries("cities", "Cities", {
///   {"alpha", 3.0, 7.0, 120.0},
///   {"beta", 1.0, 5.0, 80.0},
///   {"reference", 2.0, 6.0, std::nullopt},
/// })});
/// @endcode
class ScatterChartData {
public:
  explicit ScatterChartData(std::vector<ScatterSeries> series = {});
  /// Borrows series and their unordered samples without discarding hidden series.
  /// @return An immutable span in declaration order, valid while the snapshot's shared storage remains alive.
  [[nodiscard]] std::span<const ScatterSeries> Series() const noexcept;
  /// Changes one series' visibility while sharing its immutable samples and lookup index.
  /// @param key An existing stable series key.
  /// @param visible Whether the series contributes to plotting and automatic coordinate/magnitude domains.
  /// @return The unchanged snapshot when visibility already matches, otherwise an updated snapshot value.
  /// @throws std::invalid_argument if key does not identify a series in this snapshot.
  [[nodiscard]] ScatterChartData WithSeriesVisible(std::string_view key, bool visible) const;
  bool operator==(const ScatterChartData&) const = default;
private:
  std::shared_ptr<const charts_detail::Data> data_;
  friend struct charts_detail::Access;
};

/// Stores one finite, nonnegative value displayed as a PieChart slice.
struct PieDatum {
  /// Nonempty stable identifier, unique within the chart snapshot.
  std::string key;
  /// Display text or a generated localized string resource.
  StringVariant label;
  /// Finite, nonnegative value that determines this datum's share of the pie.
  double value = 0.0;
  bool operator==(const PieDatum&) const = default;
};

/// Owns an immutable snapshot of values displayed by PieChart.
///
/// Datum keys must be nonempty and unique. Values must be finite and nonnegative. An empty or all-zero snapshot
/// renders the chart's neutral empty state; positive values are normalized without overflowing their sum.
/// @code
/// PieChartData data({{"upload", "Upload", 72.0}, {"download", "Download", 28.0}});
/// @endcode
class PieChartData {
public:
  explicit PieChartData(std::vector<PieDatum> data = {});
  /// Borrows data in the declared slice order.
  /// @return An immutable span valid while this snapshot's shared storage remains alive.
  [[nodiscard]] std::span<const PieDatum> Data() const noexcept;
  bool operator==(const PieChartData&) const = default;
private:
  std::shared_ptr<const std::vector<PieDatum>> data_ =
      std::make_shared<const std::vector<PieDatum>>();
};

/// Describes a finite numeric interval. Explicit axis/size domains require min < max.
struct ChartNumericDomain {
  /// Inclusive lower endpoint; defaults to zero.
  double min = 0.0;
  /// Inclusive upper endpoint; defaults to one.
  double max = 1.0;
  bool operator==(const ChartNumericDomain&) const = default;
};
/// Defines an explicit numeric axis tick and an optional label override.
struct ChartTick {
  /// Finite position in data coordinates, not normalized or screen coordinates.
  double value = 0.0;
  /// Literal or resource-backed text; std::nullopt uses the axis formatter.
  /// An explicitly empty string suppresses this tick's text without removing the tick.
  std::optional<StringVariant> label;
  bool operator==(const ChartTick&) const = default;
};
/// Formats a numeric tick or the corresponding numeric value in tooltip/semantic content.
///
/// The callback takes one finite double in data units and returns owned UTF-8 text. It runs synchronously on the
/// UI thread and should be deterministic, side-effect-free and inexpensive. It must not call composition hooks.
/// Supply locale-aware formatting here when the default locale-independent general notation is insufficient.
/// An empty function restores the default formatter when passed to ChartAxis::Formatter().
using ChartTickFormatter = std::function<std::string(double)>;

/// Configures a linear or categorical axis independently of its screen orientation.
///
/// The default axis is linear with automatic range, suggested tick count six, and visible line, labels and grid.
/// Empty numeric data uses [0, 1]; constant numeric domains expand deterministically. Hidden series do not affect
/// automatic ranges. A category axis uses the snapshot's category table, never a second label/order collection.
/// @code
/// auto axis = ChartAxis::Linear()
///     .Domain({0.0, 100.0})
///     .Ticks({{0.0, "Empty"}, {50.0, "Half"}, {100.0, "Full"}})
///     .Title("Capacity")
///     .Grid(false);
/// @endcode
class ChartAxis {
public:
  /// Creates the default linear axis configuration.
  /// @return A linear axis with automatic range and no forced zero inclusion.
  static ChartAxis Linear();
  /// Creates an axis whose categories and labels come from CategoryChartData.
  /// @return A categorical axis with visible line, labels and grid.
  static ChartAxis Category();
  /// Replaces automatic range calculation with an explicit numeric interval.
  /// @param value Finite, strictly increasing endpoints in data units.
  /// @return The updated axis value for further fluent configuration.
  /// @throws std::invalid_argument if this is a category axis or value has invalid endpoints.
  ChartAxis Domain(ChartNumericDomain value) &&;
  /// Requests zero inclusion when deriving an automatic linear range; explicit domains take precedence.
  /// @param value True to include zero, false to remove this additional range request.
  /// @return The updated axis. A bar/area baseline can still contribute zero independently of this flag.
  /// @throws std::invalid_argument if this is a category axis.
  ChartAxis IncludeZero(bool value = true) &&;
  /// Sets a suggested upper budget for automatic ticks or category labels.
  /// @param value A count in [2, 1000]. Layout can display fewer labels to avoid collisions.
  /// @return The updated axis. A nonempty explicit tick list takes precedence over this suggestion.
  /// @throws std::invalid_argument if value is outside [2, 1000].
  ChartAxis TickCount(std::size_t value) &&;
  /// Supplies explicit ticks for a linear axis without changing its domain.
  /// @param value At most 1000 ticks with finite, strictly increasing positions; empty restores automatic ticks.
  /// @return The updated axis. Out-of-domain ticks are omitted, and overlapping labels may be hidden.
  /// @throws std::invalid_argument if this is a category axis or the tick list is invalid.
  ChartAxis Ticks(std::vector<ChartTick> value) &&;
  /// Sets the callback used for numeric labels not overridden by an explicit tick label.
  /// @param value A ChartTickFormatter, or an empty function to restore default numeric formatting.
  /// @return The updated axis. Tooltip and semantic values also use this formatter for the corresponding axis.
  /// @throws std::invalid_argument if this is a category axis.
  ChartAxis Formatter(ChartTickFormatter value) &&;
  /// Sets the axis title, resolved through the current SDK resource environment.
  /// @param value Literal text or a generated localized resource; empty text hides the title.
  /// @return The updated axis value.
  ChartAxis Title(StringVariant value) &&;
  /// Controls the entire axis decoration without removing its scale or changing the data domain.
  /// @param value Whether the axis line, ticks, labels, title and grid may be drawn.
  /// @return The updated axis value.
  ChartAxis Visible(bool value = true) &&;
  /// Controls tick/category labels while retaining the axis line, title and configured grid.
  /// @param value Whether labels may be drawn; layout can still hide overlapping labels.
  /// @return The updated axis value.
  ChartAxis Labels(bool value = true) &&;
  /// Controls grid lines at the axis's generated or explicit tick positions.
  /// @param value Whether the grid is drawn when the axis itself is visible.
  /// @return The updated axis value.
  ChartAxis Grid(bool value = true) &&;
private:
  bool category_ = false;
  bool visible_ = true;
  bool labels_ = true;
  bool grid_ = true;
  bool include_zero_ = false;
  std::optional<ChartNumericDomain> domain_;
  std::size_t tick_count_ = 6;
  std::vector<ChartTick> ticks_;
  std::shared_ptr<const ChartTickFormatter> formatter_;
  StringVariant title_;
  friend struct charts_detail::Access;
};

/// Maps a ScatterDatum magnitude linearly to filled marker area, rather than to radius.
///
/// Missing magnitudes use ChartMarkerStyle::area. The configured area range must be finite, nonnegative and
/// nondecreasing. Invalid configuration is rejected with std::invalid_argument when the chart is composed.
/// @code
/// ScatterSizeScale scale{.domain = ChartNumericDomain{0.0, 100.0}, .min_area = 16.0F, .max_area = 400.0F};
/// @endcode
struct ScatterSizeScale {
  /// Optional explicit magnitude range with finite endpoints satisfying 0 <= min < max.
  /// Without it, visible magnitudes determine the range; equal observed magnitudes use the area-range midpoint.
  /// Magnitudes outside an explicit range clamp to the corresponding minimum or maximum area.
  std::optional<ChartNumericDomain> domain;
  /// Smallest mapped area in DIP squared; zero permits an invisible zero-area marker.
  float min_area = 24.0F;
  /// Largest mapped area in DIP squared; must be at least min_area.
  float max_area = 600.0F;
  bool operator==(const ScatterSizeScale&) const = default;
};
/// Overrides the optional point markers on lines/areas or the primary marks of a scatter chart.
/// Unset fields inherit from the chart/theme policy. An explicit transparent Brush is different from an unset field.
struct ChartMarkerStyle {
  /// Filled geometry; when unresolved, the default is chosen deterministically from the series key.
  std::optional<ChartMarkerShape> shape;
  /// Whether to draw markers. The resolved default is false for lines/areas and true for scatter.
  /// Hiding markers does not remove the underlying series from domains, interaction or accessibility.
  std::optional<bool> visible;
  /// Finite, nonnegative fixed marker area in DIP squared; the resolved default is 36.
  /// A present ScatterDatum::magnitude uses ScatterSizeScale instead.
  std::optional<float> area;
  /// Marker fill Brush; unresolved fills inherit the series line/fill policy.
  std::optional<Brush> fill;
  /// Marker outline Brush; unresolved outlines inherit the series stroke Brush.
  std::optional<Brush> stroke;
  /// Outline width, caps, joins and dashes; unresolved marker strokes have width 1 DIP.
  /// Values must satisfy the SDK StrokeStyle contract.
  std::optional<StrokeStyle> stroke_style;
  bool operator==(const ChartMarkerStyle&) const = default;
};
/// Optional shared appearance overrides for the chart's two axes.
struct ChartAxisStyle {
  /// Axis and tick-line color; inherits the SDK theme's outline color when unresolved.
  std::optional<Color> line;
  /// Grid-line color; inherits the SDK theme's high-surface-container color when unresolved.
  std::optional<Color> grid;
  /// Typography and foreground for tick/category labels, axis titles and the empty-data message.
  std::optional<TextStyle> text;
  /// Finite, nonnegative axis/tick/grid stroke width in DIPs; the resolved default is 1.
  std::optional<float> width;
  bool operator==(const ChartAxisStyle&) const = default;
};
/// Optional appearance overrides for both interactive and informational legends.
struct ChartLegendStyle {
  /// Typography and foreground of each series label; otherwise derived from SDK body-small tokens.
  std::optional<TextStyle> text;
  /// Finite, nonnegative spacing between legend entries and wrapping lines, in DIPs.
  /// The unresolved value uses the SDK theme's small spacing token.
  std::optional<float> spacing;
  bool operator==(const ChartLegendStyle&) const = default;
};
/// Optional appearance and hit-tolerance overrides for chart-owned datum interaction.
struct ChartInteractionStyle {
  /// Highlight color shared by controlled selection, hover and the focused keyboard cursor.
  /// Hover uses reduced opacity; keyboard navigation adds a theme-surface contrast underlay.
  /// Outlines follow the datum geometry, and overlapping states on one datum use the strongest state only.
  std::optional<Color> highlight;
  /// Finite, nonnegative highlight stroke width in DIPs; the resolved default is 2.
  /// Zero suppresses datum highlights, including transient anchors and keyboard underlays, without disabling input.
  std::optional<float> width;
  /// Additional hit tolerance in DIPs, in [0, 128]; the resolved default is 8.
  /// This is independent of the SDK's pointer recognition and cancellation thresholds.
  std::optional<float> hit_slop;
  bool operator==(const ChartInteractionStyle&) const = default;
};
/// Groups chart-wide decoration overrides, separate from per-series data mark styles.
///
/// Unset optional fields inherit through ChartTheme to SDK ThemeSpec tokens. Use a chart's Appearance() method
/// to override these fields for one component; use its Style() or SeriesStyle() methods for data marks.
struct ChartStyle {
  /// Plotting-rectangle fill; does not cover the title or legend. The resolved default is transparent.
  std::optional<Brush> background;
  /// Typography and foreground of the chart title, independent of axis title styling.
  std::optional<TextStyle> title;
  /// Shared axis, tick, grid and label appearance.
  ChartAxisStyle axis;
  /// Legend label appearance and wrapping-entry spacing.
  ChartLegendStyle legend;
  /// Selection, hover and keyboard-cursor appearance and hit tolerance.
  ChartInteractionStyle interaction;
  bool operator==(const ChartStyle&) const = default;
};
/// Describes line-series visuals without changing interpolation or sampling behavior.
/// Apply as the chart-wide default with LineChart::Style() or to one keyed series with LineChart::SeriesStyle().
struct LineChartStyle {
  /// Line Brush; the unresolved value is the series' stable palette color.
  std::optional<Brush> stroke;
  /// SDK stroke configuration; the resolved default is 2 DIPs with round caps and joins.
  std::optional<StrokeStyle> stroke_style;
  /// Optional point markers; their default visibility is false.
  ChartMarkerStyle marker;
  bool operator==(const LineChartStyle&) const = default;
};
/// Describes area-series visuals independently of baseline, stacking and interpolation.
struct AreaChartStyle {
  /// Area fill Brush; the unresolved value is the series palette color with reduced alpha.
  std::optional<Brush> fill;
  /// Upper-boundary Brush; use an explicit transparent Brush to suppress its visible color.
  std::optional<Brush> stroke;
  /// SDK stroke configuration for the upper boundary; the resolved default width is 2 DIPs.
  std::optional<StrokeStyle> stroke_style;
  /// Optional markers at the displayed upper boundary; their default visibility is false.
  ChartMarkerStyle marker;
  bool operator==(const AreaChartStyle&) const = default;
};
/// Describes column/bar segment visuals independently of grouping, stacking and orientation.
struct BarChartStyle {
  /// Segment fill Brush; the unresolved value is the series' stable palette color.
  std::optional<Brush> fill;
  /// Segment outline Brush; the unresolved value is the series' stable palette color.
  std::optional<Brush> stroke;
  /// SDK stroke configuration for segment outlines; the resolved default width is 2 DIPs.
  std::optional<StrokeStyle> stroke_style;
  /// Finite, nonnegative end-corner radius in DIPs; the resolved default is 3.
  /// Only exposed positive/negative stack ends are rounded, not internal joins or the baseline end.
  std::optional<float> corner_radius;
  bool operator==(const BarChartStyle&) const = default;
};
/// Describes scatter/bubble marks without changing magnitude mapping or representative sampling.
struct ScatterChartStyle {
  /// Marker geometry and paint overrides; markers are visible by default on scatter charts.
  ChartMarkerStyle marker;
  bool operator==(const ScatterChartStyle&) const = default;
};
/// Describes pie-slice paint independently of data values and slice order.
/// Apply as the chart-wide default with PieChart::Style() or to one datum with PieChart::SliceStyle().
struct PieChartStyle {
  /// Slice fill Brush; the unresolved value is selected from ChartTheme::palette using the datum's stable key.
  std::optional<Brush> fill;
  /// Slice outline Brush; the unresolved value is the surrounding SDK surface color.
  std::optional<Brush> stroke;
  /// SDK stroke configuration for slice outlines; the resolved default width is 1 DIP.
  std::optional<StrokeStyle> stroke_style;
  /// Donut hole radius as a fraction of the outer radius; the default 0 renders a solid pie.
  /// Accepted values span [0.0, 0.9]. Chart-wide via PieChart::Style() or per slice; slices
  /// render as ring segments sharing the largest resolved value.
  std::optional<float> inner_radius;
  bool operator==(const PieChartStyle&) const = default;
};

/// Supplies optional chart-specific overrides through the SDK's typed Environment.
///
/// This value does not establish a new complete visual theme. Within an existing SDK theme, resolution follows
/// ThemeSpec, ChartTheme, per-chart configuration, then keyed series overrides. Empty fields inherit; installing
/// another ChartTheme replaces that Environment value as a whole rather than merging fields across ancestors.
/// @code
/// ChartTheme overrides;
/// overrides.common.axis.width = 1.5F;
/// overrides.line.stroke_style = StrokeStyle{.width = 3.0F};
/// overrides.palette = {Color::Rgb(35, 105, 210), Color::Rgb(185, 75, 120)};
/// return MaterialTheme {
///   ProvideEnvironment(overrides, LineChart(XYChartData{}).With(Frame{.height = 280.0F})),
/// };
/// @endcode
struct ChartTheme {
  /// Shared chart, axis, legend and interaction decoration overrides.
  ChartStyle common;
  /// Default data-mark overrides for LineChart instances in this environment.
  LineChartStyle line;
  /// Default data-mark overrides for AreaChart instances in this environment.
  AreaChartStyle area;
  /// Default data-mark overrides for BarChart instances in this environment.
  BarChartStyle bar;
  /// Default data-mark overrides for ScatterChart instances in this environment.
  ScatterChartStyle scatter;
  /// Default slice paint overrides for PieChart instances in this environment.
  PieChartStyle pie;
  /// Optional series-color palette. Empty derives colors from SDK theme tokens.
  /// Selection is deterministic by stable series key, but a finite palette cannot guarantee unique colors.
  std::vector<Color> palette;
  /// Supplies the fallback value required by the SDK typed Environment contract.
  /// @return An empty override set that leaves chart defaults derived from the surrounding SDK theme.
  static ChartTheme Default() { return {}; }
  bool operator==(const ChartTheme&) const = default;
};

/// Identifies a datum independently of current vector positions, rendered samples or translated labels.
/// Keep this value in application State when controlling selection across snapshot updates.
struct ChartDatumIdentity {
  /// Stable key of the owning series.
  std::string series_key;
  /// Stable datum key within that series; not necessarily equal to its category key.
  std::string datum_key;
  bool operator==(const ChartDatumIdentity&) const = default;
};
/// Carries an actionable numeric datum's original values, before stacking or presentation sampling.
/// Missing XY ordinates do not produce actionable ChartDatumInfo values.
struct ChartXYValue {
  /// Original finite numeric horizontal coordinate.
  double x = 0.0;
  /// Original finite ordinate, not the displayed stacked upper boundary.
  double y = 0.0;
  bool operator==(const ChartXYValue&) const = default;
};
/// Carries a categorical datum's original business value without inventing a numeric x coordinate.
struct ChartCategoryValue {
  /// Original ChartCategory key; use the snapshot's category table for its localized label.
  std::string category_key;
  /// Original finite value, not a stack total or normalized fraction.
  double value = 0.0;
  bool operator==(const ChartCategoryValue&) const = default;
};
/// Carries a scatter datum's original coordinates and optional business magnitude.
struct ChartScatterValue {
  /// Original finite horizontal coordinate.
  double x = 0.0;
  /// Original finite vertical coordinate.
  double y = 0.0;
  /// Original optional magnitude, not the mapped marker area or radius.
  std::optional<double> magnitude;
  bool operator==(const ChartScatterValue&) const = default;
};
/// Discriminated original value payload shared by selection, hover, tooltips and accessibility.
/// Its alternative follows the source snapshot type, not the chart component displaying that snapshot.
using ChartDatumValue = std::variant<ChartXYValue, ChartCategoryValue, ChartScatterValue>;

/// Owns the stable identity and original data of one currently actionable chart datum.
///
/// Event arguments borrow this value for the synchronous callback; copy it to retain information afterward.
/// Indices describe only the snapshot that produced the value and must not replace stable identity.
struct ChartDatumInfo {
  /// Stable (series, datum) key pair used by controlled selection.
  ChartDatumIdentity identity;
  /// Zero-based series position in the original snapshot, including hidden series in the indexing scheme.
  std::size_t series_index = 0;
  /// Zero-based position in the series' original input array, before category ordering or display sampling.
  std::size_t datum_index = 0;
  /// Original typed business values; no display-coordinate or stacked-boundary substitution is performed.
  ChartDatumValue value;
  /// Additional per-datum fraction in [0, 1] for percent-stacked displays; absent in other modes.
  std::optional<double> normalized_value;
  bool operator==(const ChartDatumInfo&) const = default;
};
/// Describes a proposed visibility change from an interactive legend.
/// The chart does not mutate its snapshot; the application may accept or ignore the request.
struct ChartSeriesVisibilityRequest {
  /// Stable key of the series to update.
  std::string series_key;
  /// Proposed visibility, rather than a command to invert whatever state the application currently holds.
  bool visible = true;
  bool operator==(const ChartSeriesVisibilityRequest&) const = default;
};
/// Contains typed event keys registered through the chart's inherited View::On() API.
///
/// Events target the owning chart's current handler and do not bubble between chart instances.
/// Handlers run synchronously on the UI thread. Copy borrowed payloads if retaining them after the callback.
/// @code
/// XYChartData data({XYSeries("samples", "Samples", {{"a", 0.0, 1.0}, {"b", 1.0, 2.0}})});
/// const auto selected = UseState(std::optional<ChartDatumIdentity>{});
/// const auto hovered = UseState(std::optional<ChartDatumIdentity>{});
/// return LineChart(data)
///     .Selection(selected.Get())
///     .On<ChartEvents::SelectionRequested>([selected](const ChartDatumInfo& info) {
///       selected = info.identity;
///     })
///     .On<ChartEvents::HoverChanged>([hovered](const std::optional<ChartDatumInfo>& info) {
///       hovered = info ? std::optional(info->identity) : std::nullopt;
///     });
/// @endcode
struct ChartEvents {
  /// Requests selection after a successful primary activation, Enter/Space or semantic activation.
  /// The callback receives const ChartDatumInfo& and returns void; application State remains authoritative.
  /// Blank clicks and canceled/secondary/chorded pointer sequences do not emit a selection request.
  struct SelectionRequested : Event<void(const ChartDatumInfo&)> {};
  /// Reports a hover identity transition, not every pointer move over the same datum.
  /// The callback receives const std::optional<ChartDatumInfo>&; std::nullopt means no hovered datum.
  /// Touch interaction does not require or synthesize hover to select data.
  struct HoverChanged : Event<void(const std::optional<ChartDatumInfo>&)> {};
  /// Proposes an interactive legend's next visibility value.
  /// The callback receives const ChartSeriesVisibilityRequest& and returns void. Apply WithSeriesVisible()
  /// to application-owned data and supply the resulting snapshot to accept the request.
  struct SeriesVisibilityRequested : Event<void(const ChartSeriesVisibilityRequest&)> {};
};

/// Configures whether the series legend is shown, where it is placed and whether entries are actionable.
struct ChartLegendOptions {
  /// Whether to include the legend in layout; defaults to true, including for a single series.
  bool visible = true;
  /// Whether activating an entry emits ChartEvents::SeriesVisibilityRequested; defaults to true.
  /// Visibility still changes only when the application supplies an updated snapshot.
  bool interactive = true;
  /// Position outside the plotting surface; defaults to Bottom.
  ChartLegendPlacement placement = ChartLegendPlacement::Bottom;
  bool operator==(const ChartLegendOptions&) const = default;
};
/// Owns the datum and resolved display strings passed to a custom tooltip content factory.
/// No mounted node, geometry-cache reference or platform object is exposed to the factory.
struct ChartTooltipContext {
  /// Original actionable datum, including stable identity and any additional normalized fraction.
  ChartDatumInfo datum;
  /// Owning series label resolved in the chart's current resource environment.
  std::string series_label;
  /// Resolved category label or formatted numeric x value.
  std::string position_text;
  /// Formatted original y/value, with optional scatter magnitude and percent-stack percentage appended.
  /// Inspect datum.value when custom content needs independently formatted numeric fields.
  std::string value_text;
};
/// Produces informational popup content from const ChartTooltipContext& and returns a normal SDK View.
///
/// The context is borrowed during the factory call but owns its fields and may be copied into child factories.
/// Factories may run again as content or environment changes; do not depend on a particular invocation count.
/// Return informational content without nested interactive controls. Popup placement/lifetime belongs to the chart.
using ChartTooltipFactory = std::function<View(const ChartTooltipContext&)>;
/// Configures chart-owned anchored informational popups.
/// @code
/// ChartTooltipOptions tooltip{
///   .content = [](const ChartTooltipContext& context) {
///     return Column {
///       Text(context.series_label),
///       Text(context.position_text + ": " + context.value_text),
///     }.With(Padding(8.0F));
///   },
/// };
/// @endcode
struct ChartTooltipOptions {
  /// Whether hover or cursor interaction can display a tooltip; defaults to true.
  bool enabled = true;
  /// Optional custom content factory. Empty uses the chart's standard content and the SDK TooltipStyle.
  ChartTooltipFactory content;
};
/// Configures retained data-update animation without per-frame composition or State writes.
/// Stable keys match old/new data. Compatible mark topology interpolates; incompatible or clipped contours fade in.
/// SDK reduced-motion policy resolves animation immediately even when enabled is true.
struct ChartAnimationOptions {
  /// Whether eligible updates animate; disabled by default.
  bool enabled = false;
  /// SDK duration/easing specification; duration must be finite and nonnegative.
  /// The default is TweenSpec's 0.2-second EaseOut transition. A zero duration resolves immediately.
  TweenSpec update;
  bool operator==(const ChartAnimationOptions&) const = default;
};
/// Configures the chart surface's accessible summary and bounded virtual data children.
/// Detailed, exhaustive data-table access remains an application responsibility for large datasets.
struct ChartAccessibilityOptions {
  /// Literal or generated localized summary. Empty uses the visible actionable datum count or empty-data message.
  /// Representative scatter mode additionally identifies its display reduction in the generated semantic summary.
  StringVariant summary;
  /// Maximum virtual datum children in [1, 512], default 64; excludes ordinary title/legend Views.
  /// Controlled selection and keyboard cursor have priority within this budget. Invalid limits are rejected
  /// with std::invalid_argument when the chart is composed.
  std::size_t max_data_nodes = 64;
  bool operator==(const ChartAccessibilityOptions&) const = default;
};

namespace charts_detail {
/// Supplies the common fluent API inherited by Cartesian chart components.
///
/// Construct LineChart, AreaChart, BarChart or ScatterChart rather than this implementation base. Fluent methods
/// return the concrete component and preserve independent declaration values; they do not mutate mounted state.
/// @tparam Derived The concrete chart View type whose fluent return type is preserved.
template <class Derived> class ChartComponent : public View {
public:
  /// Sets a chart title rendered as an ordinary SDK Text View outside the plotting surface.
  /// @param value Literal text or a generated localized resource; empty hides the title and its layout space.
  /// @return The concrete chart declaration with the new title.
  Derived Title(StringVariant value) &&;
  /// Supplies the authoritative selected datum identity without changing data or emitting an event.
  /// @param value Stable identity to highlight, or std::nullopt for no controlled selection.
  /// @return The updated chart declaration. Missing, hidden or out-of-domain identities are not highlighted.
  /// @note SelectionRequested only proposes a change. The application must supply a new value to accept it.
  /// The transient keyboard cursor and hover remain separate from this controlled value.
  Derived Selection(std::optional<ChartDatumIdentity> value) &&;
  /// Overrides shared decoration for this chart without changing its data-mark styles.
  /// @param value Chart-wide overrides; unset fields inherit from ChartTheme and SDK ThemeSpec.
  /// @return The updated chart declaration with the supplied appearance override set.
  /// @throws std::invalid_argument during composition if a resolved style dimension is invalid.
  /// @see ChartStyle
  Derived Appearance(ChartStyle value) &&;
  /// Configures the ordinary View-based series legend.
  /// @param value Visibility, interaction and placement policy; replaces the previous legend options.
  /// @return The updated chart declaration.
  /// @see ChartLegendOptions, ChartEvents::SeriesVisibilityRequested
  Derived Legend(ChartLegendOptions value) &&;
  /// Configures anchored informational content for hover and cursor interaction.
  /// @param value Enablement and optional custom factory; an empty factory uses the standard tooltip.
  /// @return The updated chart declaration, retaining the options for the chart's composition lifetime.
  /// @see ChartTooltipOptions, ChartTooltipContext
  Derived Tooltip(ChartTooltipOptions value) &&;
  /// Configures optional keyed data-update animation driven by retained frame callbacks.
  /// @param value Enablement and SDK TweenSpec; reduced-motion policy remains authoritative.
  /// @return The updated chart declaration.
  /// @throws std::invalid_argument during composition if the duration is negative or nonfinite.
  /// @see ChartAnimationOptions
  Derived Animation(ChartAnimationOptions value) &&;
  /// Configures the chart surface's summary and bounded virtual datum semantics.
  /// @param value Summary text and virtual-child budget, independent of ordinary title/legend semantics.
  /// @return The updated chart declaration.
  /// @throws std::invalid_argument during composition if max_data_nodes is outside [1, 512].
  /// @see ChartAccessibilityOptions
  Derived Accessibility(ChartAccessibilityOptions value) &&;
protected:
  explicit ChartComponent(std::shared_ptr<const Config> config);
  /// Applies a library-internal edit to a copied configuration and rebuilds the deferred declaration.
  /// @tparam Function A callable accepting the library's mutable Config reference.
  /// @param function Synchronous edit applied only to this declaration's new configuration value.
  /// @return The updated concrete chart declaration, consuming the current fluent value.
  template <class Function> Derived Change(Function&& function);
private:
  std::shared_ptr<const Config> config_;
};
} // namespace charts_detail

/// Displays numeric or categorical line series on one retained Cartesian plotting surface.
///
/// XYChartData uses strictly increasing numeric x values; CategoryChartData uses declared category order.
/// Missing ordinates/categories split contours. Defaults are linear interpolation, automatic numeric sampling
/// and hidden point markers. Data, selection and visibility remain application-controlled.
/// @code
/// XYChartData data({XYSeries("load", "Load", {{"a", 0.0, 2.0}, {"b", 1.0, 4.0}, {"c", 2.0, 3.0}})});
/// return LineChart(data)
///     .Title("Load over time")
///     .Interpolation(ChartInterpolation::Monotone)
///     .XAxis(ChartAxis::Linear().Title("Time"))
///     .YAxis(ChartAxis::Linear().IncludeZero())
///     .Style({.marker = {.visible = true}})
///     .With(Frame{.height = 300.0F});
/// @endcode
class LineChart final : public charts_detail::ChartComponent<LineChart> {
public:
  explicit LineChart(XYChartData data);
  explicit LineChart(CategoryChartData data);
  /// Configures the horizontal axis without replacing the source snapshot.
  /// @param value A linear axis for XYChartData or a category axis for CategoryChartData.
  /// @return The updated line-chart declaration.
  /// @throws std::invalid_argument during composition if the scale does not match the snapshot type.
  LineChart XAxis(ChartAxis value) &&;
  /// Configures the numeric value axis.
  /// @param value A linear axis, including optional range, ticks, formatter and title.
  /// @return The updated line-chart declaration.
  /// @throws std::invalid_argument during composition if value is categorical.
  LineChart YAxis(ChartAxis value) &&;
  /// Selects how consecutive nonmissing points are connected.
  /// @param value Linear, monotone, step-before or step-after interpolation; defaults to Linear.
  /// @return The updated line-chart declaration.
  LineChart Interpolation(ChartInterpolation value) &&;
  /// Selects presentation sampling without aggregating or changing original values.
  /// @param value None or Auto; applies to numeric data, while category samples remain unaggregated.
  /// @return The updated line-chart declaration. Selected and cursor data retain their original identity.
  LineChart Sampling(ChartSampling value) &&;
  /// Overrides the default stroke and marker appearance for all series in this chart.
  /// @param value Override fields layered over ChartTheme::line; unset fields inherit.
  /// @return The updated line-chart declaration; keyed SeriesStyle() overrides still take precedence.
  LineChart Style(LineChartStyle value) &&;
  /// Overrides the appearance of one series, without copying that series' samples.
  /// @param key Stable key of a series present in the snapshot, including a currently hidden series.
  /// @param value Override fields layered over this chart's default line style; replaces a prior override for key.
  /// @return The updated line-chart declaration.
  /// @throws std::invalid_argument during composition if key is unknown or a resolved style is invalid.
  LineChart SeriesStyle(std::string key, LineChartStyle value) &&;
};

/// Displays baseline-filled, stacked or percent-stacked areas from numeric or categorical series.
///
/// Defaults are an independent zero baseline, linear interpolation and automatic numeric sampling. Numeric
/// stacks require identical x grids across visible series; category stacks align to the common category table.
/// Missing members break the entire stack at that position. Positive and negative stacks accumulate separately.
/// @code
/// CategoryChartData data({{"q1", "Q1"}, {"q2", "Q2"}}, {
///   CategorySeries("direct", "Direct", {{"q1", "q1", 20.0}, {"q2", "q2", 30.0}}),
///   CategorySeries("partner", "Partner", {{"q1", "q1", 10.0}, {"q2", "q2", 25.0}}),
/// });
/// return AreaChart(data)
///     .Title("Revenue mix")
///     .Mode(AreaMode::PercentStacked)
///     .Interpolation(ChartInterpolation::StepAfter)
///     .With(Frame{.height = 300.0F});
/// @endcode
class AreaChart final : public charts_detail::ChartComponent<AreaChart> {
public:
  explicit AreaChart(XYChartData data);
  explicit AreaChart(CategoryChartData data);
  /// Configures the horizontal coordinate or category axis.
  /// @param value A linear axis for XYChartData or a category axis for CategoryChartData.
  /// @return The updated area-chart declaration.
  /// @throws std::invalid_argument during composition if the scale does not match the snapshot type.
  AreaChart XAxis(ChartAxis value) &&;
  /// Configures the numeric value axis used by the displayed area boundaries.
  /// @param value A linear axis. Without an explicit domain, percent stacks use [0, 1].
  /// @return The updated area-chart declaration.
  /// @throws std::invalid_argument during composition if value is categorical.
  AreaChart YAxis(ChartAxis value) &&;
  /// Selects independent baseline fills, signed stacking or percentage stacking.
  /// @param value Area mode; defaults to Unstacked. PercentStacked requires nonnegative visible values.
  /// @return The updated area-chart declaration. Original event values are never replaced by stack totals.
  /// @throws std::invalid_argument during composition for incompatible grids, baseline/interpolation settings,
  /// negative percentage values or nonfinite stack accumulation.
  AreaChart Mode(AreaMode value) &&;
  /// Sets the reference value to which each unstacked area is filled.
  /// @param value Finite baseline in data units; defaults to zero. Stacked modes require zero.
  /// @return The updated area-chart declaration.
  /// @throws std::invalid_argument during composition if value is nonfinite or nonzero in a stacked mode.
  AreaChart Baseline(double value) &&;
  /// Selects interpolation for the area's upper boundary and step-compatible stack boundaries.
  /// @param value Linear, monotone or a step mode. Monotone is supported only for Unstacked areas.
  /// @return The updated area-chart declaration.
  /// @throws std::invalid_argument during composition if Monotone is combined with a stacked mode.
  AreaChart Interpolation(ChartInterpolation value) &&;
  /// Selects numeric area sampling while retaining shared x positions across stacks.
  /// @param value None or Auto; category data is not automatically decimated.
  /// @return The updated area-chart declaration. Sampling affects presentation, not original values or keys.
  AreaChart Sampling(ChartSampling value) &&;
  /// Overrides default fill, boundary stroke and marker appearance for every series.
  /// @param value Override fields layered over ChartTheme::area; unset fields inherit.
  /// @return The updated area-chart declaration; keyed SeriesStyle() overrides take precedence.
  AreaChart Style(AreaChartStyle value) &&;
  /// Overrides one series' fill, boundary stroke and markers.
  /// @param key Stable key of a series present in the snapshot, including a currently hidden series.
  /// @param value Override fields layered over the default area style; replaces a prior override for key.
  /// @return The updated area-chart declaration.
  /// @throws std::invalid_argument during composition if key is unknown or a resolved style is invalid.
  AreaChart SeriesStyle(std::string key, AreaChartStyle value) &&;
};

/// Displays vertical columns or horizontal bars from category/value series.
///
/// The default is vertical grouping with a zero baseline. Hidden series reflow grouped slots; missing data
/// leaves its visible series' slot empty. Stacks accumulate positive and negative values independently.
/// CategoryAxis() and ValueAxis() keep their meaning when Orientation() changes.
/// @code
/// static const CategoryChartData initial({{"a", "Product A"}, {"b", "Product B"}}, {
///   CategorySeries("sales", "Sales", {{"a-sales", "a", 24.0}, {"b-sales", "b", 36.0}}),
///   CategorySeries("returns", "Returns", {{"a-returns", "a", -3.0}, {"b-returns", "b", -4.0}}),
/// });
/// const auto data = UseState(initial);
/// return BarChart(data.Get())
///     .Orientation(ChartOrientation::Horizontal)
///     .Mode(BarMode::Stacked)
///     .ValueAxis(ChartAxis::Linear().IncludeZero().Title("Units"))
///     .On<ChartEvents::SeriesVisibilityRequested>([data](const ChartSeriesVisibilityRequest& request) {
///       data = data->WithSeriesVisible(request.series_key, request.visible);
///     })
///     .With(Frame{.height = 300.0F});
/// @endcode
class BarChart final : public charts_detail::ChartComponent<BarChart> {
public:
  explicit BarChart(CategoryChartData data);
  /// Configures the axis that locates categories, regardless of screen orientation.
  /// @param value A category axis using the snapshot's category table and labels.
  /// @return The updated bar-chart declaration.
  /// @throws std::invalid_argument during composition if value is linear.
  BarChart CategoryAxis(ChartAxis value) &&;
  /// Configures the numeric axis along which columns/bars extend.
  /// @param value A linear axis. An explicit domain limits display without relocating the original zero baseline.
  /// @return The updated bar-chart declaration.
  /// @throws std::invalid_argument during composition if value is categorical.
  BarChart ValueAxis(ChartAxis value) &&;
  /// Selects side-by-side grouping, signed stacking or percentage stacking.
  /// @param value Bar mode; defaults to Grouped. PercentStacked requires nonnegative visible values.
  /// @return The updated bar-chart declaration; percentage event payloads retain original business values.
  /// @throws std::invalid_argument during composition for negative percentage values or stack overflow.
  BarChart Mode(BarMode value) &&;
  /// Changes the screen direction of the value axis without changing category order or data identity.
  /// @param value Vertical columns or Horizontal bars; defaults to Vertical.
  /// @return The updated bar-chart declaration.
  BarChart Orientation(ChartOrientation value) &&;
  /// Overrides default segment fill, outline and exposed-end rounding for all series.
  /// @param value Override fields layered over ChartTheme::bar; unset fields inherit.
  /// @return The updated bar-chart declaration; keyed SeriesStyle() overrides take precedence.
  BarChart Style(BarChartStyle value) &&;
  /// Overrides segment appearance for a single series without changing its data.
  /// @param key Stable key of a series present in the snapshot, including a currently hidden series.
  /// @param value Override fields layered over the default bar style; replaces a prior override for key.
  /// @return The updated bar-chart declaration.
  /// @throws std::invalid_argument during composition if key is unknown or a resolved style is invalid.
  BarChart SeriesStyle(std::string key, BarChartStyle value) &&;
};

/// Displays unordered numeric data as fixed-area scatter markers or magnitude-scaled bubbles.
///
/// Both axes are linear. Exact presentation is the default; representative reduction is opt-in and never
/// aggregates the original values. Magnitudes are mapped to area using SizeScale(), not to marker radius.
/// @code
/// ScatterChartData data({ScatterSeries("cities", "Cities", {
///   {"alpha", 3.0, 7.0, 120.0}, {"beta", 1.0, 5.0, 80.0}, {"gamma", 6.0, 2.0, 40.0},
/// })});
/// return ScatterChart(data)
///     .Title("Cities")
///     .XAxis(ChartAxis::Linear().Title("Growth"))
///     .YAxis(ChartAxis::Linear().Title("Revenue"))
///     .SizeScale({.min_area = 24.0F, .max_area = 600.0F})
///     .Style({.marker = {.shape = ChartMarkerShape::Circle}})
///     .With(Frame{.height = 320.0F});
/// @endcode
class ScatterChart final : public charts_detail::ChartComponent<ScatterChart> {
public:
  explicit ScatterChart(ScatterChartData data);
  /// Configures the numeric horizontal axis.
  /// @param value A linear axis with optional domain, ticks, formatter and title.
  /// @return The updated scatter-chart declaration.
  /// @throws std::invalid_argument during composition if value is categorical.
  ScatterChart XAxis(ChartAxis value) &&;
  /// Configures the numeric vertical axis.
  /// @param value A linear axis with optional domain, ticks, formatter and title.
  /// @return The updated scatter-chart declaration.
  /// @throws std::invalid_argument during composition if value is categorical.
  ScatterChart YAxis(ChartAxis value) &&;
  /// Configures the shared magnitude-to-area mapping for bubble data across visible series.
  /// @param value Optional magnitude domain and finite, nonnegative, nondecreasing area bounds in DIP squared.
  /// @return The updated scatter-chart declaration; data without magnitude keeps its fixed marker area.
  /// @throws std::invalid_argument during composition if the explicit domain or area range is invalid.
  ScatterChart SizeScale(ScatterSizeScale value) &&;
  /// Selects exact markers or representative screen-space sampling.
  /// @param value Exact or Representative; defaults to Exact.
  /// @return The updated scatter-chart declaration. Original identity/values and keyboard data remain available.
  ScatterChart Sampling(ScatterSampling value) &&;
  /// Overrides the default marker appearance for all scatter series.
  /// @param value Marker overrides layered over ChartTheme::scatter; unset fields inherit.
  /// @return The updated scatter-chart declaration; keyed SeriesStyle() overrides take precedence.
  ScatterChart Style(ScatterChartStyle value) &&;
  /// Overrides the marker appearance of one scatter series.
  /// @param key Stable key of a series present in the snapshot, including a currently hidden series.
  /// @param value Override fields layered over the default scatter style; replaces a prior override for key.
  /// @return The updated scatter-chart declaration.
  /// @throws std::invalid_argument during composition if key is unknown or a resolved style is invalid.
  ScatterChart SeriesStyle(std::string key, ScatterChartStyle value) &&;
};

/// Displays nonnegative values as proportional circular slices.
///
/// Slice colors use the chart palette selected by stable datum key. Labels and values remain ordinary application
/// Views so callers can choose a layout that fits their use case. PieChart uses the same Title(), Appearance() and
/// Style() and Accessibility() fluent naming as the other chart components; SliceStyle() supplies keyed overrides.
/// @code
/// PieChartData data({{"upload", "Upload", 72.0}, {"download", "Download", 28.0}});
/// return PieChart(data)
///     .SliceStyle("upload", {.fill = Brush(Color::Rgb(230, 184, 102))})
///     .SliceStyle("download", {.fill = Brush(Color::Rgb(72, 118, 232))})
///     .With(Frame{.width = 96.0F, .height = 96.0F});
/// @endcode
class PieChart final : public View {
public:
  /// Creates a proportional pie from an immutable data snapshot.
  explicit PieChart(PieChartData data);
  /// Sets a title rendered as an ordinary SDK Text View above the plotting surface.
  /// @param value Literal text or a generated localized resource; empty hides the title.
  /// @return The updated pie-chart declaration.
  PieChart Title(StringVariant value) &&;
  /// Overrides shared chart decoration without changing pie-slice styles.
  /// @param value Chart-wide overrides. Pie charts apply the title typography and plotting background fields;
  /// axis and interaction fields do not apply to a pie.
  /// @return The updated pie-chart declaration.
  PieChart Appearance(ChartStyle value) &&;
  /// Overrides the generated semantic summary for assistive technology.
  /// @param value ChartAccessibilityOptions::summary; an empty value uses the generated datum labels and shares.
  /// max_data_nodes does not apply because the pie is exposed as one summarized image.
  /// @return The updated pie-chart declaration.
  PieChart Accessibility(ChartAccessibilityOptions value) &&;
  /// Overrides slice fill and outline for every datum in the chart.
  /// @param value Override fields layered over ChartTheme::pie and the resolved palette; unset fields inherit.
  /// @return The updated pie-chart declaration. Keyed SliceStyle() overrides still take precedence.
  PieChart Style(PieChartStyle value) &&;
  /// Overrides the fill and outline of one datum without copying the immutable data snapshot.
  /// @param key Stable key of a datum present in the snapshot.
  /// @param value Override fields layered over this chart's default pie style.
  /// @return The updated pie-chart declaration.
  /// @throws std::invalid_argument during composition if key is unknown or a resolved style is invalid.
  PieChart SliceStyle(std::string key, PieChartStyle value) &&;
private:
  std::shared_ptr<const charts_detail::PieConfig> config_;
  PieChart Change(std::function<void(charts_detail::PieConfig&)>&& update);
};

} // namespace huxerui
