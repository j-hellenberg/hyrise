#include "tpcc_table_generator.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "abstract_table_generator.hpp"
#include "benchmark_config.hpp"
#include "constants.hpp"
#include "storage/chunk.hpp"
#include "storage/constraints/constraint_utils.hpp"
#include "storage/mvcc_data.hpp"
#include "storage/table.hpp"
#include "storage/table_column_definition.hpp"
#include "types.hpp"

namespace hyrise {

TPCCTableGenerator::TPCCTableGenerator(size_t num_warehouses, const std::shared_ptr<BenchmarkConfig>& benchmark_config)
    : AbstractTableGenerator(benchmark_config), _num_warehouses(num_warehouses) {}

TPCCTableGenerator::TPCCTableGenerator(size_t num_warehouses, ChunkOffset chunk_size)
    : AbstractTableGenerator(std::make_shared<BenchmarkConfig>(chunk_size)), _num_warehouses(num_warehouses) {}

std::shared_ptr<Table> TPCCTableGenerator::generate_item_table() {
  auto cardinalities = std::make_shared<std::vector<size_t>>(std::initializer_list<size_t>{NUM_ITEMS});

  /**
   * indices[0] = item
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  auto original_ids = _random_gen.select_unique_ids(NUM_ITEMS / 10, NUM_ITEMS);

  _add_column<int32_t>(segments_by_chunk, column_definitions, "I_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "I_IM_ID", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return _random_gen.random_number(1, 10'000);
                       });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "I_NAME", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(14, 24)};
                          });
  _add_column<float>(segments_by_chunk, column_definitions, "I_PRICE", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return static_cast<float>(_random_gen.random_number(100, 10'000)) / 100.f;
                     });
  _add_column<pmr_string>(
      segments_by_chunk, column_definitions, "I_DATA", cardinalities, [&](const std::vector<size_t>& indices) {
        auto data = _random_gen.astring(26, 50);
        const auto is_original = original_ids.find(indices[0]) != original_ids.end();
        if (is_original) {
          const auto original_string = std::string{"ORIGINAL"};
          const auto start_pos = _random_gen.random_number(0, data.length() - 1 - original_string.length());
          data.replace(start_pos, original_string.length(), original_string);
        }
        return pmr_string{data};
      });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

std::shared_ptr<Table> TPCCTableGenerator::generate_warehouse_table() {
  auto cardinalities = std::make_shared<std::vector<size_t>>(std::initializer_list<size_t>{_num_warehouses});

  /**
   * indices[0] = warehouse
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  _add_column<int32_t>(segments_by_chunk, column_definitions, "W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "W_NAME", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(6, 10)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "W_STREET_1", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "W_STREET_2", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "W_CITY", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "W_STATE", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(2, 2)};
                          });

  _add_column<pmr_string>(segments_by_chunk, column_definitions, "W_ZIP", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.zip_code()};
                          });
  _add_column<float>(segments_by_chunk, column_definitions, "W_TAX", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return static_cast<float>(_random_gen.random_number(0, 2'000)) / 10'000.f;
                     });
  _add_column<float>(segments_by_chunk, column_definitions, "W_YTD", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return CUSTOMER_YTD * NUM_CUSTOMERS_PER_DISTRICT * NUM_DISTRICTS_PER_WAREHOUSE;
                     });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

std::shared_ptr<Table> TPCCTableGenerator::generate_stock_table() {
  auto cardinalities = std::make_shared<std::vector<size_t>>(
      std::initializer_list<size_t>{_num_warehouses, NUM_STOCK_ITEMS_PER_WAREHOUSE});

  /**
   * indices[0] = warehouse
   * indices[1] = stock
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  auto original_ids = _random_gen.select_unique_ids(NUM_ITEMS / 10, NUM_ITEMS);

  _add_column<int32_t>(segments_by_chunk, column_definitions, "S_I_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[1] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "S_W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "S_QUANTITY", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return _random_gen.random_number(10, 100);
                       });
  for (auto district_i = int32_t{1}; district_i <= 10; district_i++) {
    auto district_i_str = std::stringstream{};
    district_i_str << std::setw(2) << std::setfill('0') << district_i;
    _add_column<pmr_string>(segments_by_chunk, column_definitions, "S_DIST_" + district_i_str.str(), cardinalities,
                            [&](const std::vector<size_t>& /*indices*/) {
                              return pmr_string{_random_gen.astring(24, 24)};
                            });
  }
  _add_column<int32_t>(segments_by_chunk, column_definitions, "S_YTD", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return 0;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "S_ORDER_CNT", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return 0;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "S_REMOTE_CNT", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return 0;
                       });
  _add_column<pmr_string>(
      segments_by_chunk, column_definitions, "S_DATA", cardinalities, [&](const std::vector<size_t>& indices) {
        auto data = _random_gen.astring(26, 50);
        const auto is_original = original_ids.find(indices[1]) != original_ids.end();
        if (is_original) {
          const auto original_string = std::string{"ORIGINAL"};
          const auto start_pos = _random_gen.random_number(0, data.length() - 1 - original_string.length());
          data.replace(start_pos, original_string.length(), original_string);
        }
        return pmr_string{data};
      });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

std::shared_ptr<Table> TPCCTableGenerator::generate_district_table() {
  auto cardinalities = std::make_shared<std::vector<size_t>>(
      std::initializer_list<size_t>{_num_warehouses, NUM_DISTRICTS_PER_WAREHOUSE});

  /**
   * indices[0] = warehouse
   * indices[1] = district
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  _add_column<int32_t>(segments_by_chunk, column_definitions, "D_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[1] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "D_W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "D_NAME", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(6, 10)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "D_STREET_1", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "D_STREET_2", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "D_CITY", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "D_STATE", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(2, 2)};
                          });

  _add_column<pmr_string>(segments_by_chunk, column_definitions, "D_ZIP", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.zip_code()};
                          });
  _add_column<float>(segments_by_chunk, column_definitions, "D_TAX", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return static_cast<float>(_random_gen.random_number(0, 2'000)) / 10'000.f;
                     });
  _add_column<float>(segments_by_chunk, column_definitions, "D_YTD", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return CUSTOMER_YTD * NUM_CUSTOMERS_PER_DISTRICT;
                     });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "D_NEXT_O_ID", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return NUM_ORDERS_PER_DISTRICT + 1;
                       });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

std::shared_ptr<Table> TPCCTableGenerator::generate_customer_table() {
  auto cardinalities = std::make_shared<std::vector<size_t>>(
      std::initializer_list<size_t>{_num_warehouses, NUM_DISTRICTS_PER_WAREHOUSE, NUM_CUSTOMERS_PER_DISTRICT});

  /**
   * indices[0] = warehouse
   * indices[1] = district
   * indices[2] = customer
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  auto original_ids = _random_gen.select_unique_ids(NUM_ITEMS / 10, NUM_ITEMS);

  _add_column<int32_t>(segments_by_chunk, column_definitions, "C_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[2] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "C_D_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[1] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "C_W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_FIRST", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(8, 16)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_MIDDLE", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{"OE"};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_LAST", cardinalities,
                          [&](const std::vector<size_t>& indices) {
                            return pmr_string{_random_gen.last_name(indices[2])};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_STREET_1", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_STREET_2", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_CITY", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(10, 20)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_STATE", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(2, 2)};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_ZIP", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.zip_code()};
                          });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_PHONE", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.nstring(16, 16)};
                          });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "C_SINCE", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return _current_date;
                       });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_CREDIT", cardinalities,
                          [&](const std::vector<size_t>& indices) {
                            const auto is_original = original_ids.find(indices[2]) != original_ids.end();
                            return pmr_string{is_original ? "BC" : "GC"};
                          });
  _add_column<float>(segments_by_chunk, column_definitions, "C_CREDIT_LIM", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return 50'000;
                     });
  _add_column<float>(segments_by_chunk, column_definitions, "C_DISCOUNT", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return static_cast<float>(_random_gen.random_number(0, 5'000)) / 10'000.f;
                     });
  _add_column<float>(segments_by_chunk, column_definitions, "C_BALANCE", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return -CUSTOMER_YTD;
                     });
  _add_column<float>(segments_by_chunk, column_definitions, "C_YTD_PAYMENT", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return CUSTOMER_YTD;
                     });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "C_PAYMENT_CNT", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "C_DELIVERY_CNT", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return 0;
                       });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "C_DATA", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(300, 500)};
                          });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  _random_gen.reset_c_for_c_last();

  return table;
}

std::shared_ptr<Table> TPCCTableGenerator::generate_history_table() {
  auto cardinalities = std::make_shared<std::vector<size_t>>(std::initializer_list<size_t>{
      _num_warehouses, NUM_DISTRICTS_PER_WAREHOUSE, NUM_CUSTOMERS_PER_DISTRICT, NUM_HISTORY_ENTRIES_PER_CUSTOMER});

  /**
   * indices[0] = warehouse
   * indices[1] = district
   * indices[2] = customer
   * indices[3] = history
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  _add_column<int32_t>(segments_by_chunk, column_definitions, "H_C_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[2] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "H_C_D_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[1] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "H_C_W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "H_D_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[1] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "H_W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "H_DATE", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return _current_date;
                       });
  _add_column<float>(segments_by_chunk, column_definitions, "H_AMOUNT", cardinalities,
                     [&](const std::vector<size_t>& /*indices*/) {
                       return 10.f;
                     });
  _add_column<pmr_string>(segments_by_chunk, column_definitions, "H_DATA", cardinalities,
                          [&](const std::vector<size_t>& /*indices*/) {
                            return pmr_string{_random_gen.astring(12, 24)};
                          });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

std::shared_ptr<Table> TPCCTableGenerator::generate_order_table(
    const TPCCTableGenerator::OrderLineCounts& order_line_counts) {
  auto cardinalities = std::make_shared<std::vector<size_t>>(
      std::initializer_list<size_t>{_num_warehouses, NUM_DISTRICTS_PER_WAREHOUSE, NUM_ORDERS_PER_DISTRICT});

  /**
   * indices[0] = warehouse
   * indices[1] = district
   * indices[2] = order
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  // TODO(anyone): generate a new customer permutation for each district and warehouse. Currently they all have the
  // same permutation
  auto customer_permutation = _random_gen.permutation(0, NUM_CUSTOMERS_PER_DISTRICT);

  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[2] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_D_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[1] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_C_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return customer_permutation[indices[2]] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_ENTRY_D", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return _current_date;
                       });

  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_CARRIER_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[2] + 1 <= NUM_ORDERS_PER_DISTRICT - NUM_NEW_ORDERS_PER_DISTRICT
                                    ? std::optional<int32_t>{_random_gen.random_number(1, 10)}
                                    : std::nullopt;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_OL_CNT", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return order_line_counts[indices[0]][indices[1]][indices[2]];
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "O_ALL_LOCAL", cardinalities,
                       [&](const std::vector<size_t>& /*indices*/) {
                         return 1;
                       });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

TPCCTableGenerator::OrderLineCounts TPCCTableGenerator::generate_order_line_counts() const {
  auto order_line_counts = OrderLineCounts(_num_warehouses);
  for (auto& counts_per_warehouse : order_line_counts) {
    counts_per_warehouse.resize(NUM_DISTRICTS_PER_WAREHOUSE);
    for (auto& counts_per_district : counts_per_warehouse) {
      counts_per_district.resize(NUM_ORDERS_PER_DISTRICT);
      for (auto& count_per_order : counts_per_district) {
        count_per_order = _random_gen.random_number(5, 15);
      }
    }
  }
  return order_line_counts;
}

/**
 * Generates a column for the 'ORDER_LINE' table. This is used in the specialization of add_column to insert vectors.
 * In contrast to other tables the ORDER_LINE table is NOT defined by saying, there are 10 order_lines per order,
 * but instead there 5 to 15 order_lines per order.
 */
template <typename T>
std::vector<std::optional<T>> TPCCTableGenerator::_generate_inner_order_line_column(
    const std::vector<size_t>& indices, TPCCTableGenerator::OrderLineCounts order_line_counts,
    const std::function<std::optional<T>(const std::vector<size_t>&)>& generator_function) {
  auto order_line_count = order_line_counts[indices[0]][indices[1]][indices[2]];

  auto values = std::vector<std::optional<T>>{};
  values.reserve(order_line_count);
  for (auto index = size_t{0}; index < order_line_count; index++) {
    auto copied_indices = indices;
    copied_indices.push_back(index);
    values.push_back(generator_function(copied_indices));
  }

  return values;
}

template <typename T>
void TPCCTableGenerator::_add_order_line_column(
    std::vector<Segments>& segments_by_chunk, TableColumnDefinitions& column_definitions, std::string name,
    std::shared_ptr<std::vector<size_t>> cardinalities, TPCCTableGenerator::OrderLineCounts order_line_counts,
    const std::function<std::optional<T>(const std::vector<size_t>&)>& generator_function) {
  const std::function<std::vector<std::optional<T>>(const std::vector<size_t>&)> wrapped_generator_function =
      [&](const std::vector<size_t>& indices) {
        return _generate_inner_order_line_column(indices, order_line_counts, generator_function);
      };
  _add_column<T>(segments_by_chunk, column_definitions, name, cardinalities, wrapped_generator_function);
}

std::shared_ptr<Table> TPCCTableGenerator::generate_order_line_table(
    const TPCCTableGenerator::OrderLineCounts& order_line_counts) {
  auto cardinalities = std::make_shared<std::vector<size_t>>(
      std::initializer_list<size_t>{_num_warehouses, NUM_DISTRICTS_PER_WAREHOUSE, NUM_ORDERS_PER_DISTRICT});

  /**
   * indices[0] = warehouse
   * indices[1] = district
   * indices[2] = order
   * indices[3] = order_line_size
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_O_ID", cardinalities, order_line_counts,
                                  [&](const std::vector<size_t>& indices) {
                                    return indices[2] + 1;
                                  });
  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_D_ID", cardinalities, order_line_counts,
                                  [&](const std::vector<size_t>& indices) {
                                    return indices[1] + 1;
                                  });
  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_W_ID", cardinalities, order_line_counts,
                                  [&](const std::vector<size_t>& indices) {
                                    return indices[0] + 1;
                                  });
  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_NUMBER", cardinalities, order_line_counts,
                                  [&](const std::vector<size_t>& indices) {
                                    return indices[3] + 1;
                                  });
  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_I_ID", cardinalities, order_line_counts,
                                  [&](const std::vector<size_t>& /*indices*/) {
                                    return _random_gen.random_number(1, NUM_ITEMS);
                                  });
  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_SUPPLY_W_ID", cardinalities,
                                  order_line_counts, [&](const std::vector<size_t>& indices) {
                                    return indices[0] + 1;
                                  });
  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_DELIVERY_D", cardinalities,
                                  order_line_counts, [&](const std::vector<size_t>& indices) {
                                    return indices[2] + 1 <= NUM_ORDERS_PER_DISTRICT - NUM_NEW_ORDERS_PER_DISTRICT
                                               ? std::optional<int32_t>{_current_date}
                                               : std::nullopt;
                                  });
  _add_order_line_column<int32_t>(segments_by_chunk, column_definitions, "OL_QUANTITY", cardinalities,
                                  order_line_counts, [&](const std::vector<size_t>& /*indices*/) {
                                    return 5;
                                  });

  _add_order_line_column<float>(segments_by_chunk, column_definitions, "OL_AMOUNT", cardinalities, order_line_counts,
                                [&](const std::vector<size_t>& indices) {
                                  return indices[2] < NUM_ORDERS_PER_DISTRICT - NUM_NEW_ORDERS_PER_DISTRICT
                                             ? 0.f
                                             : static_cast<float>(_random_gen.random_number(1, 999999)) / 100.f;
                                });
  _add_order_line_column<pmr_string>(segments_by_chunk, column_definitions, "OL_DIST_INFO", cardinalities,
                                     order_line_counts, [&](const std::vector<size_t>& /*indices*/) {
                                       return pmr_string{_random_gen.astring(24, 24)};
                                     });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

std::shared_ptr<Table> TPCCTableGenerator::generate_new_order_table() {
  auto cardinalities = std::make_shared<std::vector<size_t>>(
      std::initializer_list<size_t>{_num_warehouses, NUM_DISTRICTS_PER_WAREHOUSE, NUM_NEW_ORDERS_PER_DISTRICT});

  /**
   * indices[0] = warehouse
   * indices[1] = district
   * indices[2] = new_order
   */
  auto segments_by_chunk = std::vector<Segments>{};
  auto column_definitions = TableColumnDefinitions{};

  _add_column<int32_t>(segments_by_chunk, column_definitions, "NO_O_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[2] + 1 + NUM_ORDERS_PER_DISTRICT - NUM_NEW_ORDERS_PER_DISTRICT;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "NO_D_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[1] + 1;
                       });
  _add_column<int32_t>(segments_by_chunk, column_definitions, "NO_W_ID", cardinalities,
                       [&](const std::vector<size_t>& indices) {
                         return indices[0] + 1;
                       });

  auto table =
      std::make_shared<Table>(column_definitions, TableType::Data, _benchmark_config->chunk_size, UseMvcc::Yes);
  for (const auto& segments : segments_by_chunk) {
    const auto mvcc_data = std::make_shared<MvccData>(segments.front()->size(), CommitID{0});
    table->append_chunk(segments, mvcc_data);
  }

  return table;
}

std::unordered_map<std::string, BenchmarkTableInfo> TPCCTableGenerator::generate() {
  const auto cache_directory = std::string{"tpcc_cached_tables/sf-"} + std::to_string(_num_warehouses);  // NOLINT
  if (_benchmark_config->cache_binary_tables && std::filesystem::is_directory(cache_directory)) {
    return _load_binary_tables_from_path(cache_directory);
  }

  const auto item_table = generate_item_table();
  const auto warehouse_table = generate_warehouse_table();
  const auto stock_table = generate_stock_table();
  const auto district_table = generate_district_table();
  const auto customer_table = generate_customer_table();
  const auto history_table = generate_history_table();
  const auto new_order_table = generate_new_order_table();

  const auto order_line_counts = generate_order_line_counts();
  const auto order_table = generate_order_table(order_line_counts);
  const auto order_line_table = generate_order_line_table(order_line_counts);

  auto table_info_by_name =
      std::unordered_map<std::string, BenchmarkTableInfo>({{"ITEM", BenchmarkTableInfo{item_table}},
                                                           {"WAREHOUSE", BenchmarkTableInfo{warehouse_table}},
                                                           {"STOCK", BenchmarkTableInfo{stock_table}},
                                                           {"DISTRICT", BenchmarkTableInfo{district_table}},
                                                           {"CUSTOMER", BenchmarkTableInfo{customer_table}},
                                                           {"HISTORY", BenchmarkTableInfo{history_table}},
                                                           {"ORDER", BenchmarkTableInfo{order_table}},
                                                           {"ORDER_LINE", BenchmarkTableInfo{order_line_table}},
                                                           {"NEW_ORDER", BenchmarkTableInfo{new_order_table}}});

  if (_benchmark_config->cache_binary_tables) {
    std::filesystem::create_directories(cache_directory);
    for (auto& [table_name, table_info] : table_info_by_name) {
      // NOLINTNEXTLINE(performance-inefficient-string-concatenation)
      table_info.binary_file_path = cache_directory + "/" + table_name + ".bin";
    }
  }

  return table_info_by_name;
}

AbstractTableGenerator::IndexesByTable TPCCTableGenerator::_indexes_by_table() const {
  return {{"CUSTOMER", {{"C_ID"}, {"C_D_ID"}, {"C_W_ID"}}},
          {"DISTRICT", {{"D_ID"}, {"D_W_ID"}}},
          {"STOCK", {{"S_W_ID"}, {"S_I_ID"}}},
          {"ORDER_LINE", {{"OL_W_ID"}, {"OL_D_ID"}, {"OL_O_ID"}, {"OL_NUMBER"}}},
          {"ITEM", {{"I_ID"}}},
          {"NEW_ORDER", {{"NO_O_ID"}, {"NO_D_ID"}, {"NO_W_ID"}}},
          {"ORDER", {{"O_ID"}, {"O_D_ID"}, {"O_W_ID"}}},
          {"WAREHOUSE", {{"W_ID"}}}};
}

void TPCCTableGenerator::_add_constraints(
    std::unordered_map<std::string, BenchmarkTableInfo>& table_info_by_name) const {
  // Set all primary (PK) and foreign keys (FK) as defined in the specification (Revision 5.11, 1.3 Table Layouts,
  // p. 12-17).

  // Get all tables.
  const auto& warehouse_table = table_info_by_name.at("WAREHOUSE").table;
  const auto& district_table = table_info_by_name.at("DISTRICT").table;
  const auto& customer_table = table_info_by_name.at("CUSTOMER").table;
  const auto& history_table = table_info_by_name.at("HISTORY").table;
  const auto& new_order_table = table_info_by_name.at("NEW_ORDER").table;
  const auto& order_table = table_info_by_name.at("ORDER").table;
  const auto& order_line_table = table_info_by_name.at("ORDER_LINE").table;
  const auto& item_table = table_info_by_name.at("ITEM").table;
  const auto& stock_table = table_info_by_name.at("STOCK").table;

  // Set constraints.

  // WAREHOUSE - 1 PK.
  primary_key_constraint(warehouse_table, {"W_ID"});

  // DISTRICT - 1 composite PK, 1 FK.
  primary_key_constraint(district_table, {"D_W_ID", "D_ID"});
  foreign_key_constraint(district_table, {"D_W_ID"}, warehouse_table, {"W_ID"});

  // CUSTOMER - 1 composite PK, 1 composite FK.
  primary_key_constraint(customer_table, {"C_W_ID", "C_D_ID", "C_ID"});
  foreign_key_constraint(customer_table, {"C_W_ID", "C_D_ID"}, district_table, {"D_W_ID", "D_ID"});

  // HISTORY - 2 composite FKs.
  foreign_key_constraint(history_table, {"H_C_W_ID", "H_C_D_ID", "H_C_ID"}, customer_table,
                         {"C_W_ID", "C_D_ID", "C_ID"});
  foreign_key_constraint(history_table, {"H_W_ID", "H_D_ID"}, district_table, {"D_W_ID", "D_ID"});

  // NEW_ORDER - 1 composite PK, 1 composite FK.
  primary_key_constraint(new_order_table, {"NO_W_ID", "NO_D_ID", "NO_O_ID"});
  foreign_key_constraint(new_order_table, {"NO_W_ID", "NO_D_ID", "NO_O_ID"}, order_table, {"O_W_ID", "O_D_ID", "O_ID"});

  // ORDER - 1 composite PK, 1 composite FK.
  primary_key_constraint(order_table, {"O_W_ID", "O_D_ID", "O_ID"});
  foreign_key_constraint(order_table, {"O_W_ID", "O_D_ID", "O_C_ID"}, customer_table, {"C_W_ID", "C_D_ID", "C_ID"});

  // ORDER_LINE - 1 composite PK, 2 composite FKs.
  primary_key_constraint(order_line_table, {"OL_W_ID", "OL_D_ID", "OL_O_ID", "OL_NUMBER"});
  foreign_key_constraint(order_line_table, {"OL_W_ID", "OL_D_ID", "OL_O_ID"}, order_table,
                         {"O_W_ID", "O_D_ID", "O_ID"});
  foreign_key_constraint(order_line_table, {"OL_SUPPLY_W_ID", "OL_I_ID"}, stock_table, {"S_W_ID", "S_I_ID"});

  // ITEM - 1 PK.
  primary_key_constraint(item_table, {"I_ID"});

  // STOCK - 1 composite PK, 2 FKs.
  primary_key_constraint(stock_table, {"S_W_ID", "S_I_ID"});
  foreign_key_constraint(stock_table, {"S_W_ID"}, warehouse_table, {"W_ID"});
  foreign_key_constraint(stock_table, {"S_I_ID"}, item_table, {"I_ID"});
}

thread_local TPCCRandomGenerator TPCCTableGenerator::_random_gen;  // NOLINT

}  // namespace hyrise
