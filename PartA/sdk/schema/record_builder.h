#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "sdk/data/field_value.h"
#include "sdk/schema/is_vector.h"
#include "sdk/schema/required.h"

namespace tracking
{
    template <class E>
    class RecordBuilder;

    template <class E>
    std::vector<std::pair<std::string, FieldValue>> FieldsOf(const E& event);

    template <class T>
    FieldValue ToValue(const T& source)
    {
        if constexpr (std::is_enum_v<T>)
        {
            const std::string_view name = ToString(source);
            return FieldValue(name);
        }
        else if constexpr (IsVector<T>::value)
        {
            std::vector<FieldValue> items;
            items.reserve(source.size());
            for (const auto& item : source)
            {
                FieldValue item_value = ToValue(item);
                items.push_back(std::move(item_value));
            }
            return FieldValue(std::move(items));
        }
        else if constexpr (requires(RecordBuilder<T>& builder) { T::Describe(builder); })
        {
            std::vector<std::pair<std::string, FieldValue>> fields = FieldsOf(source);
            return FieldValue(std::move(fields));
        }
        else
        {
            return FieldValue(source);
        }
    }

    template <class E>
    class RecordBuilder
    {
    public:
        explicit RecordBuilder(const E& event) : event_(event)
        {
        }

        template <class M>
        void operator()(std::string_view name, M E::* member)
        {
            Add(name, event_.*member);
        }
        std::vector<std::pair<std::string, FieldValue>> Take()
        {
            return std::move(out_);
        }

    private:
        template <class T>
        void Add(std::string_view name, const Required<T>& field)
        {
            FieldValue value = ToValue(field.value);
            out_.emplace_back(std::string(name), std::move(value));
        }

        // An unset optional is left out. "Not provided" never becomes 0 or "".
        template <class T>
        void Add(std::string_view name, const std::optional<T>& field)
        {
            if (field.has_value())
            {
                FieldValue value = ToValue(*field);
                out_.emplace_back(std::string(name), std::move(value));
            }
        }

        const E& event_;
        std::vector<std::pair<std::string, FieldValue>> out_;
    };

    // The named values of one event, in declaration order.
    template <class E>
    std::vector<std::pair<std::string, FieldValue>> FieldsOf(const E& event)
    {
        RecordBuilder<E> builder(event);
        E::Describe(builder);
        return builder.Take();
    }
} // namespace tracking
