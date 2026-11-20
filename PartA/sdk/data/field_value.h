#pragma once

#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace tracking
{
    // One value of an event: null, bool, int64, double, string, array, or object. A value can hold a list of namedvalues.
    class FieldValue
    {
    public:
        FieldValue() = default;

        // Constrained templates so each C++ type lands on exactly one alternative
        template <std::same_as<bool> B>
        FieldValue(B flag) : data_(flag)
        {
        }
        template <std::integral I>
        requires(std::same_as<I, bool> == false)
        FieldValue(I number) : data_(static_cast<std::int64_t>(number))
        {
        }
        template <std::floating_point F>
        FieldValue(F number) : data_(static_cast<double>(number))
        {
        }

        FieldValue(const char* text) : data_(std::string(text))
        {
        }
        FieldValue(std::string_view text) : data_(std::string(text))
        {
        }
        FieldValue(std::string text) : data_(std::move(text))
        {
        }
        FieldValue(std::vector<FieldValue> items) : data_(std::move(items))
        {
        }
        FieldValue(std::vector<std::pair<std::string, FieldValue>> fields) : data_(std::move(fields))
        {
        }

        template <class T>
        const T* As() const
        {
            return std::get_if<T>(&data_);
        }
        bool IsNull() const
        {
            return std::holds_alternative<std::monostate>(data_);
        }
        const std::variant<std::monostate, bool, std::int64_t, double, std::string, std::vector<FieldValue>, std::vector<std::pair<std::string, FieldValue>>>& variant() const
        {
            return data_;
        }

    private:
        // Named values are a vector of pairs. A vector keeps field order, and std::vector is guaranteed to accept a type that is still incomplete here. std::map is not.
        std::variant<std::monostate, bool, std::int64_t, double, std::string, std::vector<FieldValue>, std::vector<std::pair<std::string, FieldValue>>> data_;
    };
} // namespace tracking
