/*
 * Copyright (c) 2026, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Types.h>

namespace JS::Bytecode {

class Interpreter;

class AsmInterpreter {
public:
#ifdef AK_OS_RINOS
    static void run(Interpreter&, size_t)
    {
        __builtin_trap();
    }

    static bool is_available()
    {
        return false;
    }
#else
    static void run(Interpreter&, size_t entry_point);
    static bool is_available();
#endif
};

}
