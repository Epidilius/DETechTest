// A minimal test harness, so the project needs no third-party framework.
#pragma once

#include <iostream>

namespace test
{

    inline int checks = 0;
    inline int failures = 0;

    inline void Check(bool ok, const char* file, int line)
    {
        ++checks;
        if (ok == false)
        {
            ++failures;
            std::cerr << file << "(" << line << "): CHECK failed\n";
        }
    }

} // namespace test

// Reports the file and line of a failed check. The condition is not turned into text, because raw string literals do not survive that on every compiler.
#define CHECK(condition) test::Check(static_cast<bool>(condition), __FILE__, __LINE__)

// Defined in pipeline_tests.cpp.
void RunPipelineTests();
