#include "service/validator.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/schema/field_type.h"

namespace tracking
{
    std::optional<std::string> Validator::CheckValue(const FieldSpec& spec, const FieldValue& value, const std::string& path)
    {
        const std::optional<std::string> wrong_type = "wrong type at " + path;
        switch (spec.type)
        {
            case FieldType::Bool:
            {
                return value.As<bool>() != nullptr ? std::nullopt : wrong_type;
            }
            case FieldType::Int:
            {
                return value.As<std::int64_t>() != nullptr ? std::nullopt : wrong_type;
            }
            case FieldType::Double:
            {
                const double* number = value.As<double>();
                if (number == nullptr)
                {
                    return wrong_type;
                }
                if (std::isfinite(*number) == false)
                {
                    return "not a finite number at " + path;
                }

                return std::nullopt;
            }
            case FieldType::String:
            {
                return value.As<std::string>() != nullptr ? std::nullopt : wrong_type;
            }
            case FieldType::Array:
            {
                const std::vector<FieldValue>* items = value.As<std::vector<FieldValue>>();
                if (items == nullptr)
                {
                    return wrong_type;
                }

                for (std::size_t i = 0; i < items->size(); ++i)
                {
                    const std::string item_path = path + "[" + std::to_string(i) + "]";
                    const std::optional<std::string> problem = CheckValue(spec.children.front(), (*items)[i], item_path);
                    if (problem.has_value())
                    {
                        return problem;
                    }
                }

                return std::nullopt;
            }
            case FieldType::Object:
            {
                const std::vector<std::pair<std::string, FieldValue>>* object = value.As<std::vector<std::pair<std::string, FieldValue>>>();
                return object != nullptr ? CheckFields(spec.children, *object, path + ".") : wrong_type;
            }
        }

        return wrong_type;
    }

    std::optional<std::string> Validator::CheckFields(const std::vector<FieldSpec>& schema, const std::vector<std::pair<std::string, FieldValue>>& fields, const std::string& path)
    {
        for (const auto& [name, value] : fields)
        {
            const FieldSpec* spec = nullptr;
            for (const auto& candidate : schema)
            {
                if (candidate.name == name)
                {
                    spec = &candidate;
                }
            }

            if (spec == nullptr)
            {
                return "unknown field " + path + name;
            }

            const std::optional<std::string> problem = CheckValue(*spec, value, path + name);
            if (problem.has_value())
            {
                return problem;
            }
        }

        for (const auto& spec : schema)
        {
            if (spec.required == false)
            {
                continue;
            }

            bool found = false;
            for (const auto& [name, value] : fields)
            {
                found = found || name == spec.name;
            }

            if (found == false)
            {
                return "missing required field " + path + spec.name;
            }
        }
        return std::nullopt;
    }

    std::optional<std::string> Validator::Validate(const std::vector<FieldSpec>& schema, const std::vector<std::pair<std::string, FieldValue>>& fields)
    {
        return CheckFields(schema, fields, "");
    }
} // namespace tracking
