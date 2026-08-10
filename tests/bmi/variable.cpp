#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "bmi/internal.hpp"

namespace {

using TestValues = std::variant<std::vector<float>, std::vector<double>, std::vector<int>>;

struct TestVariable {
    TestVariable(std::string name, std::string type,
                 const std::vector<double>& values, bool is_input, bool is_output);

    std::string name_;
    std::string type_;
    bool is_input_{false};
    bool is_output_{false};
    int item_size_{0};
    int byte_count_{0};
    TestValues values_;
};

// Return the native scalar size for one supported test variable type
int test_item_size(const std::string& type) {
    if (type == "float") {
        return static_cast<int>(sizeof(float));
    }
    if (type == "double") {
        return static_cast<int>(sizeof(double));
    }
    if (type == "int") {
        return static_cast<int>(sizeof(int));
    }
    throw std::runtime_error("unsupported test BMI type '" + type + "'");
}

// Construct typed values from one common numeric representation
TestVariable::TestVariable(std::string name, std::string type,
                           const std::vector<double>& values, bool is_input,
                           bool is_output)
    : name_(std::move(name)), type_(std::move(type)), is_input_(is_input),
      is_output_(is_output), item_size_(test_item_size(type_)),
      byte_count_(item_size_ * static_cast<int>(values.size())) {
    if (type_ == "float") {
        std::vector<float> converted;
        converted.reserve(values.size());
        for (const double value : values) {
            converted.push_back(static_cast<float>(value));
        }
        values_ = std::move(converted);
    } else if (type_ == "double") {
        values_ = values;
    } else if (type_ == "int") {
        std::vector<int> converted;
        converted.reserve(values.size());
        for (const double value : values) {
            converted.push_back(static_cast<int>(value));
        }
        values_ = std::move(converted);
    }
}

class TestBmi final : public ::bmi::Bmi {
  public:
    explicit TestBmi(std::vector<TestVariable> variables)
        : variables_(std::move(variables)) {}

    TestVariable& variable(const std::string& name) {
        const std::vector<TestVariable>::iterator variable_it =
            std::find_if(variables_.begin(), variables_.end(),
                         [&name](const TestVariable& variable) {
                             return variable.name_ == name;
                         });
        if (variable_it == variables_.end()) {
            throw std::runtime_error("unknown test BMI variable '" + name + "'");
        }
        return *variable_it;
    }

    const TestVariable& variable(const std::string& name) const {
        const std::vector<TestVariable>::const_iterator variable_it =
            std::find_if(variables_.begin(), variables_.end(),
                         [&name](const TestVariable& variable) {
                             return variable.name_ == name;
                         });
        if (variable_it == variables_.end()) {
            throw std::runtime_error("unknown test BMI variable '" + name + "'");
        }
        return *variable_it;
    }

    std::vector<double> values(const std::string& name) const {
        std::vector<double> result;
        std::visit(
            [&result](const auto& typed_values) {
                result.reserve(typed_values.size());
                for (const auto value : typed_values) {
                    result.push_back(static_cast<double>(value));
                }
            },
            variable(name).values_);
        return result;
    }

    // Supply minimal lifecycle and clock behaviour required by the BMI interface
    void Initialize(std::string /*config_file*/) override {}

    void Update() override {
        current_time_ += 1.0;
    }

    void UpdateUntil(double time) override {
        current_time_ = time;
    }

    void Finalize() override {}

    std::string GetComponentName() override {
        return "TestBmi";
    }

    // Expose configured input and output variable metadata
    int GetInputItemCount() override {
        return static_cast<int>(
            std::count_if(variables_.begin(), variables_.end(),
                          [](const TestVariable& variable) { return variable.is_input_; }));
    }

    int GetOutputItemCount() override {
        return static_cast<int>(std::count_if(variables_.begin(), variables_.end(),
                                              [](const TestVariable& variable) {
                                                  return variable.is_output_;
                                              }));
    }

    std::vector<std::string> GetInputVarNames() override {
        return variable_names(true);
    }

    std::vector<std::string> GetOutputVarNames() override {
        return variable_names(false);
    }

    int GetVarGrid(std::string name) override {
        variable(name);
        return 0;
    }

    std::string GetVarType(std::string name) override {
        return variable(name).type_;
    }

    std::string GetVarUnits(std::string name) override {
        variable(name);
        return "1";
    }

    int GetVarItemsize(std::string name) override {
        return variable(name).item_size_;
    }

    int GetVarNbytes(std::string name) override {
        return variable(name).byte_count_;
    }

    std::string GetVarLocation(std::string name) override {
        variable(name);
        return "node";
    }

    double GetCurrentTime() override {
        return current_time_;
    }

    double GetStartTime() override {
        return 0.0;
    }

    double GetEndTime() override {
        return 1.0;
    }

    std::string GetTimeUnits() override {
        return "s";
    }

    double GetTimeStep() override {
        return 1.0;
    }

    // Copy complete typed buffers for coupling and output tests
    void GetValue(std::string name, void* destination) override {
        std::visit(
            [destination](const auto& typed_values) {
                using Value = typename std::decay_t<decltype(typed_values)>::value_type;
                std::copy(typed_values.begin(), typed_values.end(),
                          static_cast<Value*>(destination));
            },
            variable(name).values_);
    }

    void* GetValuePtr(std::string /*name*/) override {
        throw std::runtime_error("GetValuePtr not supported by TestBmi");
    }

    void GetValueAtIndices(std::string /*name*/, void* /*destination*/, int* /*indices*/,
                           int /*count*/) override {
        throw std::runtime_error("GetValueAtIndices not supported by TestBmi");
    }

    void SetValue(std::string name, void* source) override {
        std::visit(
            [source](auto& typed_values) {
                using Value = typename std::decay_t<decltype(typed_values)>::value_type;
                const Value* source_values = static_cast<const Value*>(source);
                std::copy(source_values, source_values + typed_values.size(),
                          typed_values.begin());
            },
            variable(name).values_);
    }

    void SetValueAtIndices(std::string /*name*/, int* /*indices*/, int /*count*/,
                           void* /*source*/) override {
        throw std::runtime_error("SetValueAtIndices not supported by TestBmi");
    }

    // Supply unused scalar grid metadata required by the BMI interface
    int GetGridRank(const int /*grid*/) override {
        return 1;
    }

    int GetGridSize(const int /*grid*/) override {
        return 1;
    }

    std::string GetGridType(const int /*grid*/) override {
        return "uniform_rectilinear";
    }

    void GetGridShape(const int /*grid*/, int* shape) override {
        *shape = 1;
    }

    void GetGridSpacing(const int /*grid*/, double* spacing) override {
        *spacing = 1.0;
    }

    void GetGridOrigin(const int /*grid*/, double* origin) override {
        *origin = 0.0;
    }

    void GetGridX(const int /*grid*/, double* x) override {
        *x = 0.0;
    }

    void GetGridY(const int /*grid*/, double* y) override {
        *y = 0.0;
    }

    void GetGridZ(const int /*grid*/, double* z) override {
        *z = 0.0;
    }

    int GetGridNodeCount(const int /*grid*/) override {
        return 1;
    }

    int GetGridEdgeCount(const int /*grid*/) override {
        return 0;
    }

    int GetGridFaceCount(const int /*grid*/) override {
        return 0;
    }

    void GetGridEdgeNodes(const int /*grid*/, int* /*edge_nodes*/) override {}

    void GetGridFaceEdges(const int /*grid*/, int* /*face_edges*/) override {}

    void GetGridFaceNodes(const int /*grid*/, int* /*face_nodes*/) override {}

    void GetGridNodesPerFace(const int /*grid*/, int* /*nodes_per_face*/) override {}

  private:
    std::vector<std::string> variable_names(bool inputs) const {
        std::vector<std::string> names;
        for (const TestVariable& variable : variables_) {
            if ((inputs && variable.is_input_) || (!inputs && variable.is_output_)) {
                names.push_back(variable.name_);
            }
        }
        return names;
    }

    std::vector<TestVariable> variables_;
    double current_time_{0.0};
};

// Read one complete text file as ordered lines
std::vector<std::string> read_lines(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("unable to open test output '" + path.string() + "'");
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }
    return lines;
}

} // namespace

// Write flattened values for every supported CSV output type
BOOST_AUTO_TEST_CASE(multi_item_output_preserves_type_and_item_order) {
    const std::filesystem::path output_directory =
        std::filesystem::path(MIAOW_BMI_TEST_OUTPUT_ROOT) / "variable_output";
    std::filesystem::remove_all(output_directory);
    std::filesystem::create_directories(output_directory);

    TestBmi model({
        TestVariable("double_values", "double", {1.25, 2.5}, false, true),
        TestVariable("float_values", "float", {3.5, 4.5}, false, true),
        TestVariable("int_values", "int", {5.0, 6.0}, false, true),
    });

    // Destroy the writer before reading its flushed artifact
    std::unique_ptr<miaow::bmi::OutputWriter> writer =
        std::make_unique<miaow::bmi::CsvOutputWriter>(
            model, output_directory.string(), "arrays",
            std::vector<std::string>{"double_values", "float_values", "int_values"});
    writer->write_step(model, 0.0);
    writer.reset();

    const std::vector<std::string> lines = read_lines(output_directory / "arrays.csv");
    BOOST_REQUIRE_EQUAL(lines.size(), 2);
    BOOST_TEST(lines[0] == "time,double_values_0,double_values_1,float_values_0,"
                           "float_values_1,int_values_0,int_values_1");
    BOOST_TEST(lines[1] == "0,1.25,2.5,3.5,4.5,5,6");

    std::filesystem::remove_all(output_directory);
}

// Transfer equal-count arrays through every supported source-target type pair
BOOST_AUTO_TEST_CASE(equal_count_coupling_preserves_flattened_values) {
    const std::vector<std::pair<std::string, std::string>> type_pairs{
        {"double", "double"},
        {"float", "float"},
        {"double", "float"},
        {"float", "double"},
    };
    const std::vector<double> expected{1.25, -2.5, 3.75};

    for (const std::pair<std::string, std::string>& type_pair : type_pairs) {
        BOOST_TEST_CONTEXT(type_pair.first << " to " << type_pair.second) {
            TestBmi source(
                {TestVariable("source", type_pair.first, expected, false, true)});
            TestBmi target({TestVariable("target", type_pair.second, {0.0, 0.0, 0.0},
                                         true, false)});

            const miaow::bmi::BmiCoupling coupling(0, source, "source", 1, target,
                                                   "target");
            coupling.apply(source, target);

            BOOST_TEST(target.values("target") == expected,
                       boost::test_tools::per_element());
        }
    }
}

// Reject unsupported endpoint types and unequal flattened counts
BOOST_AUTO_TEST_CASE(coupling_rejects_invalid_endpoints) {
    TestBmi scalar_source({TestVariable("source", "double", {1.0}, false, true)});
    TestBmi array_target({TestVariable("target", "double", {0.0, 0.0}, true, false)});
    BOOST_CHECK_EXCEPTION(
        miaow::bmi::BmiCoupling(0, scalar_source, "source", 1, array_target, "target"),
        std::runtime_error, [](const std::runtime_error& error) {
            return std::string(error.what()).find("requires equal item counts") !=
                   std::string::npos;
        });

    TestBmi int_source({TestVariable("source", "int", {1.0}, false, true)});
    TestBmi double_target({TestVariable("target", "double", {0.0}, true, false)});
    BOOST_CHECK_EXCEPTION(
        miaow::bmi::BmiCoupling(0, int_source, "source", 1, double_target, "target"),
        std::runtime_error, [](const std::runtime_error& error) {
            return std::string(error.what()).find("source variable 'source' must have") !=
                   std::string::npos;
        });

    TestBmi double_source({TestVariable("source", "double", {1.0}, false, true)});
    TestBmi int_target({TestVariable("target", "int", {0.0}, true, false)});
    BOOST_CHECK_EXCEPTION(
        miaow::bmi::BmiCoupling(0, double_source, "source", 1, int_target, "target"),
        std::runtime_error, [](const std::runtime_error& error) {
            return std::string(error.what()).find("target variable 'target' must have") !=
                   std::string::npos;
        });
}

// Reject inconsistent item-size and byte-count metadata
BOOST_AUTO_TEST_CASE(variable_constructor_rejects_invalid_metadata) {
    TestVariable wrong_item_size("value", "double", {1.0}, false, true);
    wrong_item_size.item_size_ = static_cast<int>(sizeof(float));
    TestBmi wrong_item_size_model({std::move(wrong_item_size)});
    BOOST_CHECK_EXCEPTION(miaow::bmi::BmiVariable(wrong_item_size_model, "value",
                                                  "BMI output error: "),
                          std::runtime_error, [](const std::runtime_error& error) {
                              return std::string(error.what()).find("reports item size") !=
                                     std::string::npos;
                          });

    TestVariable non_positive_bytes("value", "double", {1.0}, false, true);
    non_positive_bytes.byte_count_ = 0;
    TestBmi non_positive_bytes_model({std::move(non_positive_bytes)});
    BOOST_CHECK_EXCEPTION(
        miaow::bmi::BmiVariable(non_positive_bytes_model, "value", "BMI output error: "),
        std::runtime_error, [](const std::runtime_error& error) {
            return std::string(error.what()).find("non-positive byte count") !=
                   std::string::npos;
        });

    TestVariable non_divisible_bytes("value", "double", {1.0}, false, true);
    non_divisible_bytes.byte_count_ = static_cast<int>(sizeof(double)) + 1;
    TestBmi non_divisible_bytes_model({std::move(non_divisible_bytes)});
    BOOST_CHECK_EXCEPTION(
        miaow::bmi::BmiVariable(non_divisible_bytes_model, "value", "BMI output error: "),
        std::runtime_error, [](const std::runtime_error& error) {
            return std::string(error.what()).find("not divisible by item size") !=
                   std::string::npos;
        });
}
