#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "sdk/schema/field_spec.h"
#include "sdk/schema/field_type.h"
#include "sdk/schema/is_vector.h"
#include "sdk/schema/unwrap.h"

namespace tracking
{
    template <class E>
    class SchemaBuilder;

    template <class E>
    std::vector<FieldSpec> SchemaOf();

    template <class T>
    FieldSpec SpecOf(std::string_view name, bool required)
    {
        FieldSpec spec;
        spec.name = std::string(name);
        spec.required = required;
        if constexpr (std::same_as<T, bool>)
        {
            spec.type = FieldType::Bool;
        }
        else if constexpr (std::integral<T>)
        {
            spec.type = FieldType::Int;
        }
        else if constexpr (std::floating_point<T>)
        {
            spec.type = FieldType::Double;
        }
        else if constexpr (std::is_enum_v<T> || std::convertible_to<T, std::string_view>)
        {
            spec.type = FieldType::String;
        }
        else if constexpr (IsVector<T>::value)
        {
            spec.type = FieldType::Array;
            FieldSpec element = SpecOf<typename T::value_type>("", true);
            spec.children.push_back(std::move(element));
        }
        else if constexpr (requires(SchemaBuilder<T>& builder) { T::Describe(builder); })
        {
            spec.type = FieldType::Object;
            spec.children = SchemaOf<T>();
        }
        else
        {
            static_assert(sizeof(T) == 0, "no schema type for this member");
        }
        return spec;
    }

    template <class E>
    class SchemaBuilder
    {
    public:
        template <class M>
        void operator()(std::string_view name, M E::*)
        {
            FieldSpec spec = SpecOf<typename Unwrap<M>::type>(name, Unwrap<M>::required);
            specs_.push_back(std::move(spec));
        }
        std::vector<FieldSpec> Take()
        {
            return std::move(specs_);
        }

    private:
        std::vector<FieldSpec> specs_;
    };

    // The shape of event E: field names, types, and which are required.
    template <class E>
    std::vector<FieldSpec> SchemaOf()
    {
        SchemaBuilder<E> builder;
        E::Describe(builder);
        return builder.Take();
    }
} // namespace tracking
