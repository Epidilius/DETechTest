#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "sdk/data/field_value.h"
#include "sdk/schema/field_spec.h"

namespace tracking
{
    // One generic checker for every event. Driven by a schema that was generated from the event's struct.
    class Validator
    {
    public:
        static std::optional<std::string> Validate(const std::vector<FieldSpec>& schema, const std::vector<std::pair<std::string, FieldValue>>& fields);

    private:
        // `path` says where the value sits in the event, such as "squad[0].".
        static std::optional<std::string> CheckFields(const std::vector<FieldSpec>& schema, const std::vector<std::pair<std::string, FieldValue>>& fields, const std::string& path);
        static std::optional<std::string> CheckValue(const FieldSpec& spec, const FieldValue& value, const std::string& path);
    };
} // namespace tracking
