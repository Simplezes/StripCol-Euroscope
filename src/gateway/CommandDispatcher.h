#pragma once

#include "gateway/CommandContext.h"
#include <string>

namespace CommandDispatcher
{
    void Dispatch(CommandContext &context, const std::string &rawMessage);
}
