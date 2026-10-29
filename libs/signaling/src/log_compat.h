#pragma once

// Signaling transport diagnostics are delivered through callbacks in S2.
// Replace these no-op macros with the project logging subsystem when it lands.
#define TALK_LOG_DEBUG(...) do { } while (false)
#define TALK_LOG_INFO(...) do { } while (false)
#define TALK_LOG_WARN(...) do { } while (false)
#define TALK_LOG_ERROR(...) do { } while (false)