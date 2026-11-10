#pragma once

#ifdef MA4_CRASH_REPORTING
#include "util/ExceptionHandler.h"

#define MA4_ERROR_HANDLER_CONCAT_INNER(a, b) a##b
#define MA4_ERROR_HANDLER_CONCAT(a, b) MA4_ERROR_HANDLER_CONCAT_INNER(a, b)

#define BEGIN_ERROR_HANDLER                                                                                    \
    DebugExceptionHandler::ErrorBoundaryScope MA4_ERROR_HANDLER_CONCAT(ma4ErrorBoundaryScope, __LINE__); \
    try {
#define END_ERROR_HANDLER                                                                \
    }                                                                                    \
    catch (const std::exception& e) {                                                    \
        LogExceptionDetails(e);                                                          \
        DebugExceptionHandler::AbortProcess();                                           \
    }                                                                                    \
    catch (...) {                                                                        \
        LogUnknownExceptionDetails("Caught unknown exception at ma4 error boundary"); \
        DebugExceptionHandler::AbortProcess();                                           \
    }

#else
#define BEGIN_ERROR_HANDLER
#define END_ERROR_HANDLER
#endif
