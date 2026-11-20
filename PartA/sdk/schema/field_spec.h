#pragma once

#include <string>
#include <vector>

#include "sdk/schema/field_type.h"

namespace tracking
{
    struct FieldSpec
    {
        std::string name;
        FieldType type = FieldType::String;
        bool required = true;
        std::vector<FieldSpec> children;
    };
} // namespace tracking
