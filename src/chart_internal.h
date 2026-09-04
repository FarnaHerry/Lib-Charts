#pragma once

#include <huxerui/charts.h>
#include <huxerui/presentation.h>
#include <huxerui/theme.h>

#include <limits>
#include <unordered_map>

namespace huxerui::charts_detail {

enum class Kind { Line, Area, Bar, Scatter };
enum class DataKind { XY, Category, Scatter };

struct Sample {
  double x = 0.0;
  std::optional<double> y;
  std::optional<double> magnitude;
  std::size_t original_index = 0;
};
struct SampleIndex {
  std::vector<Sample> samples;
  std::unordered_map<std::string, std::size_t> by_key;
};
struct Entry {
  std::string key;
  StringVariant label;
  bool visible = true;
  std::shared_ptr<const SampleIndex> index;
};
struct Data {
  DataKind kind = DataKind::XY;
  std::vector<XYSeries> xy;
  std::vector<CategorySeries> category;
  std::vector<ScatterSeries> scatter;
  std::shared_ptr<const std::vector<ChartCategory>> categories = std::make_shared<const std::vector<ChartCategory>>();
  std::vector<Entry> entries;
};

struct Access {
  template <class T> static auto DataOf(const T& value) { return value.data_; }
  static bool Category(const ChartAxis& axis) { return axis.category_; }
  static bool Visible(const ChartAxis& axis) { return axis.visible_; }
  static bool Labels(const ChartAxis& axis) { return axis.labels_; }
  static bool Grid(const ChartAxis& axis) { return axis.grid_; }
  static bool Zero(const ChartAxis& axis) { return axis.include_zero_; }
  static auto Domain(const ChartAxis& axis) { return axis.domain_; }
  static auto Count(const ChartAxis& axis) { return axis.tick_count_; }
  static const auto& Ticks(const ChartAxis& axis) { return axis.ticks_; }
  static const auto& Formatter(const ChartAxis& axis) { return axis.formatter_; }
  static const auto& Title(const ChartAxis& axis) { return axis.title_; }
  static bool Equal(const ChartAxis& a, const ChartAxis& b) {
    return a.category_ == b.category_ && a.visible_ == b.visible_ && a.labels_ == b.labels_ &&
           a.grid_ == b.grid_ && a.include_zero_ == b.include_zero_ && a.domain_ == b.domain_ &&
           a.tick_count_ == b.tick_count_ && a.ticks_ == b.ticks_ && a.formatter_ == b.formatter_ && a.title_ == b.title_;
  }
};

using SeriesStyle = std::variant<LineChartStyle, AreaChartStyle, BarChartStyle, ScatterChartStyle>;
struct Config {
  Kind kind = Kind::Line;
  std::shared_ptr<const Data> data;
  ChartAxis x = ChartAxis::Linear();
  ChartAxis y = ChartAxis::Linear();
  ChartInterpolation interpolation = ChartInterpolation::Linear;
  AreaMode area_mode = AreaMode::Unstacked;
  BarMode bar_mode = BarMode::Grouped;
  ChartOrientation orientation = ChartOrientation::Vertical;
  double baseline = 0.0;
  ChartSampling sampling = ChartSampling::Auto;
  ScatterSampling scatter_sampling = ScatterSampling::Exact;
  ScatterSizeScale size_scale;
  StringVariant title;
  std::optional<ChartDatumIdentity> selection;
  ChartStyle appearance;
  SeriesStyle style = LineChartStyle{};
  std::unordered_map<std::string, SeriesStyle> series_styles;
  ChartLegendOptions legend;
  std::shared_ptr<const ChartTooltipOptions> tooltip = std::make_shared<const ChartTooltipOptions>();
  ChartAnimationOptions animation;
  ChartAccessibilityOptions accessibility;
};

struct ModelSample {
  double x = 0.0;
  double lower = 0.0;
  double upper = 0.0;
  std::size_t index = 0;
  bool gap = false;
  bool exposed = true;
  std::optional<double> normalized;
};
struct Model {
  std::vector<std::vector<ModelSample>> series;
  ChartNumericDomain x;
  ChartNumericDomain y;
  ChartNumericDomain magnitude;
  std::size_t count = 0;
};

struct NumericPoint {
  double x = 0.0;
  double y = 0.0;
};
struct Mark {
  std::size_t series = 0;
  std::size_t sample = 0;
  Point point;
  Point baseline;
  NumericPoint source;
  NumericPoint source_baseline;
  Rect bounds;
  float area = 0.0F;
  ChartMarkerShape shape = ChartMarkerShape::Circle;
  bool exposed = true;
  std::optional<double> normalized;
};
struct CurveSegment {
  Point start;
  Point end;
  std::size_t start_mark = 0;
  std::size_t end_mark = 0;
};
struct Contour {
  std::size_t series = 0;
  std::vector<std::size_t> marks;
  Path line;
  Path fill;
  /// Clipped rendered segments are also the hit-test curve; no synthetic datum identities are introduced.
  std::vector<CurveSegment> segments;
};
struct Geometry {
  Kind kind = Kind::Line;
  Rect plot;
  ChartNumericDomain x_domain;
  ChartNumericDomain y_domain;
  std::vector<Mark> marks;
  std::vector<Contour> contours;
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> cells;
};

void Require(bool condition, const char* message);
void ValidateDomain(ChartNumericDomain domain);
void ValidateConfig(const Config& config);
bool SameModel(const Config& a, const Config& b);
Model BuildModel(const Config& config);
// Visible series own group slots even when they have no samples in the current category.
std::optional<Mark> ProjectMark(const Config& config, const Model& model, Rect plot, std::size_t series,
                                const ModelSample& sample, const ChartMarkerStyle& marker,
                                std::size_t slot, std::size_t visible_count);
Geometry BuildGeometry(const Config& config, const Model& model, Rect plot,
                       const std::vector<ChartMarkerStyle>& markers,
                       std::span<const ChartDatumIdentity> pinned = {});
Geometry InterpolateGeometry(const Geometry& from, const Geometry& to, float progress, ChartInterpolation interpolation);
void RefreshHitIndex(Geometry& geometry);
Path MarkerPath(Point center, float area, ChartMarkerShape shape);
Rect MarkerBounds(Point center, float area, ChartMarkerShape shape);
std::optional<std::size_t> Hit(const Geometry& geometry, Point point, float slop);
ChartDatumInfo DatumInfo(const Data& data, const Mark& mark);
std::string Number(double value);
ChartNumericDomain AutoDomain(double min, double max, bool zero);
double Fraction(double value, ChartNumericDomain domain);
std::uint64_t StableHash(std::string_view value);
View BuildChart(std::shared_ptr<const Config> config);

} // namespace huxerui::charts_detail
