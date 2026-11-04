#include "chart_internal.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace huxerui::charts_detail {

void Require(bool condition, const char* message) {
  if (!condition) throw std::invalid_argument(message);
}

void ValidateDomain(ChartNumericDomain domain) {
  Require(std::isfinite(domain.min) && std::isfinite(domain.max) && domain.min < domain.max,
          "Chart domain must have finite, strictly increasing endpoints");
}

// Normalize typed input into a shared numeric index without changing the application's sample arrays.
// Geometry uses index positions; original_index preserves the mapping back to public values and event indices.
template <class Series> void IndexSeries(Data& data, const std::vector<Series>& series) {
  std::unordered_set<std::string> series_keys;
  std::unordered_map<std::string, std::size_t> categories;
  for (std::size_t i = 0; i < data.categories->size(); ++i) {
    const auto& category = (*data.categories)[i];
    Require(!category.key.empty() && categories.emplace(category.key, i).second,
            "Chart category keys must be nonempty and unique");
  }
  for (const auto& item : series) {
    Require(!item.Key().empty() && series_keys.insert(item.Key()).second,
            "Chart series keys must be nonempty and unique");
    auto index = std::make_shared<SampleIndex>();
    std::unordered_set<std::string> category_keys;
    index->samples.reserve(item.Samples().size());
    std::size_t ordinal = 0;
    double previous = -std::numeric_limits<double>::infinity();
    for (const auto& datum : item.Samples()) {
      Require(!datum.key.empty() && index->by_key.emplace(datum.key, ordinal).second,
              "Chart datum keys must be nonempty and unique within a series");
      Sample sample{.original_index = ordinal++};
      if constexpr (std::is_same_v<Series, CategorySeries>) {
        auto found = categories.find(datum.category_key);
        Require(found != categories.end(), "Chart datum references an unknown category");
        Require(category_keys.insert(datum.category_key).second, "A series can contain only one datum per category");
        sample.x = static_cast<double>(found->second);
        sample.y = datum.value;
      } else {
        sample.x = datum.x;
        sample.y = datum.y;
        if constexpr (std::is_same_v<Series, XYSeries>) {
          Require(sample.x > previous, "XY series x values must be strictly increasing");
          previous = sample.x;
        } else {
          sample.magnitude = datum.magnitude;
          Require(!sample.magnitude || (std::isfinite(*sample.magnitude) && *sample.magnitude >= 0.0),
                  "Scatter magnitude must be finite and nonnegative");
        }
      }
      Require(std::isfinite(sample.x) && (!sample.y || std::isfinite(*sample.y)),
              "Chart coordinates must be finite; use a missing XY ordinate for a gap");
      index->samples.push_back(sample);
    }
    if constexpr (std::is_same_v<Series, CategorySeries>) {
      // Category order controls plotting, not input order. Rebuild key lookups after sorting the index only.
      std::sort(index->samples.begin(), index->samples.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
      for (std::size_t i = 0; i < index->samples.size(); ++i) {
        index->by_key[item.Samples()[index->samples[i].original_index].key] = i;
      }
    }
    data.entries.push_back({item.Key(), item.Label(), item.Visible(), std::move(index)});
  }
}

std::shared_ptr<const Data> Visibility(const std::shared_ptr<const Data>& data, std::string_view key, bool visible) {
  const auto found = std::find_if(data->entries.begin(), data->entries.end(), [&](const Entry& e) { return e.key == key; });
  Require(found != data->entries.end(), "Unknown chart series key");
  // Preserve snapshot identity for no-op updates so the retained chart can reuse its model.
  if (found->visible == visible) return data;
  // Copy metadata while retaining shared samples and indices; public series and internal entries must agree.
  auto result = std::make_shared<Data>(*data);
  const auto i = static_cast<std::size_t>(found - data->entries.begin());
  result->entries[i].visible = visible;
  if (data->kind == DataKind::XY) result->xy[i] = result->xy[i].WithVisible(visible);
  if (data->kind == DataKind::Category) result->category[i] = result->category[i].WithVisible(visible);
  if (data->kind == DataKind::Scatter) result->scatter[i] = result->scatter[i].WithVisible(visible);
  return result;
}

// Events report original business values, never clipped coordinates or accumulated stack heights.
// A percentage is additional presentation metadata rather than a replacement for the original value.
ChartDatumInfo DatumInfo(const Data& data, const Mark& mark) {
  const auto& entry = data.entries.at(mark.series);
  const auto& sample = entry.index->samples.at(mark.sample);
  ChartDatumInfo result{.identity = {entry.key, {}}, .series_index = mark.series,
                       .datum_index = sample.original_index, .normalized_value = mark.normalized};
  if (data.kind == DataKind::XY) {
    const auto& datum = data.xy[mark.series].Samples()[sample.original_index];
    result.identity.datum_key = datum.key;
    result.value = ChartXYValue{datum.x, *datum.y};
  } else if (data.kind == DataKind::Category) {
    const auto& datum = data.category[mark.series].Samples()[sample.original_index];
    result.identity.datum_key = datum.key;
    result.value = ChartCategoryValue{datum.category_key, datum.value};
  } else {
    const auto& datum = data.scatter[mark.series].Samples()[sample.original_index];
    result.identity.datum_key = datum.key;
    result.value = ChartScatterValue{datum.x, datum.y, datum.magnitude};
  }
  return result;
}

} // namespace huxerui::charts_detail

namespace huxerui {

XYChartData::XYChartData(std::vector<XYSeries> series) {
  auto data = std::make_shared<charts_detail::Data>();
  data->xy = std::move(series);
  charts_detail::IndexSeries(*data, data->xy);
  data_ = std::move(data);
}
std::span<const XYSeries> XYChartData::Series() const noexcept { return data_->xy; }
XYChartData XYChartData::WithSeriesVisible(std::string_view key, bool visible) const {
  auto result = *this;
  result.data_ = charts_detail::Visibility(data_, key, visible);
  return result;
}
CategoryChartData::CategoryChartData(std::vector<ChartCategory> categories, std::vector<CategorySeries> series) {
  auto data = std::make_shared<charts_detail::Data>();
  data->kind = charts_detail::DataKind::Category;
  data->categories = std::make_shared<const std::vector<ChartCategory>>(std::move(categories));
  data->category = std::move(series);
  charts_detail::IndexSeries(*data, data->category);
  data_ = std::move(data);
}
std::span<const ChartCategory> CategoryChartData::Categories() const noexcept { return *data_->categories; }
std::span<const CategorySeries> CategoryChartData::Series() const noexcept { return data_->category; }
CategoryChartData CategoryChartData::WithSeriesVisible(std::string_view key, bool visible) const {
  auto result = *this;
  result.data_ = charts_detail::Visibility(data_, key, visible);
  return result;
}
ScatterChartData::ScatterChartData(std::vector<ScatterSeries> series) {
  auto data = std::make_shared<charts_detail::Data>();
  data->kind = charts_detail::DataKind::Scatter;
  data->scatter = std::move(series);
  charts_detail::IndexSeries(*data, data->scatter);
  data_ = std::move(data);
}
std::span<const ScatterSeries> ScatterChartData::Series() const noexcept { return data_->scatter; }
ScatterChartData ScatterChartData::WithSeriesVisible(std::string_view key, bool visible) const {
  auto result = *this;
  result.data_ = charts_detail::Visibility(data_, key, visible);
  return result;
}

PieChartData::PieChartData(std::vector<PieDatum> data) {
  std::unordered_set<std::string> keys;
  for (const auto& datum : data) {
    charts_detail::Require(!datum.key.empty() && keys.insert(datum.key).second,
                           "Pie datum keys must be nonempty and unique");
    charts_detail::Require(std::isfinite(datum.value) && datum.value >= 0.0,
                           "Pie datum values must be finite and nonnegative");
  }
  data_ = std::make_shared<const std::vector<PieDatum>>(std::move(data));
}

std::span<const PieDatum> PieChartData::Data() const noexcept { return *data_; }

ChartAxis ChartAxis::Linear() { return {}; }
ChartAxis ChartAxis::Category() { ChartAxis axis; axis.category_ = true; return axis; }
ChartAxis ChartAxis::Domain(ChartNumericDomain value) && {
  charts_detail::Require(!category_, "Category axes do not accept a numeric domain");
  charts_detail::ValidateDomain(value);
  domain_ = value;
  return std::move(*this);
}
ChartAxis ChartAxis::IncludeZero(bool value) && {
  charts_detail::Require(!category_, "IncludeZero is only available for linear axes");
  include_zero_ = value;
  return std::move(*this);
}
ChartAxis ChartAxis::TickCount(std::size_t value) && {
  charts_detail::Require(value >= 2 && value <= 1000, "Chart tick count must be between 2 and 1000");
  tick_count_ = value;
  return std::move(*this);
}
ChartAxis ChartAxis::Ticks(std::vector<ChartTick> value) && {
  charts_detail::Require(!category_ && value.size() <= 1000, "Explicit ticks require a linear axis and at most 1000 ticks");
  double previous = -std::numeric_limits<double>::infinity();
  for (const auto& tick : value) {
    charts_detail::Require(std::isfinite(tick.value) && tick.value > previous, "Explicit ticks must be finite and increasing");
    previous = tick.value;
  }
  ticks_ = std::move(value);
  return std::move(*this);
}
ChartAxis ChartAxis::Formatter(ChartTickFormatter value) && {
  charts_detail::Require(!category_, "Category labels come from ChartCategory");
  formatter_ = value ? std::make_shared<const ChartTickFormatter>(std::move(value)) : nullptr;
  return std::move(*this);
}
ChartAxis ChartAxis::Title(StringVariant value) && { title_ = std::move(value); return std::move(*this); }
ChartAxis ChartAxis::Visible(bool value) && { visible_ = value; return std::move(*this); }
ChartAxis ChartAxis::Labels(bool value) && { labels_ = value; return std::move(*this); }
ChartAxis ChartAxis::Grid(bool value) && { grid_ = value; return std::move(*this); }

} // namespace huxerui
