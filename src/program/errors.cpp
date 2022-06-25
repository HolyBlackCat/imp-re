#include "errors.h"

#include <csignal>
#include <cstddef>
#include <exception>
#include <type_traits>

#include "interface/messagebox.h"
#include "program/exit.h"

namespace Program
{
    static void SignalHandler(int sig)
    {
        switch (sig)
        {
          case SIGABRT:
          case SIGINT:
          case SIGTERM:
            Exit(0);
            break;
          case SIGFPE:
            HardError("Signal: Floating point exception.");
            break;
          case SIGILL:
            HardError("Signal: Illegal instruction.");
            break;
          case SIGSEGV:
            HardError("Signal: Segmentation fault.");
            break;
          default:
            HardError("Signal: Unknown.");
            break;
        }
    }

    static void TerminateHandler()
    {
        if (std::exception_ptr e = std::current_exception())
        {
            try
            {
                std::rethrow_exception(e);
            }
            catch (std::exception &e)
            {
                std::string error;
                ExceptionToString(e, [&](const char *message)
                {
                    if (!error.empty())
                        error += "\n";
                    error += message;
                });
                HardError(error);
            }
            catch (...)
            {
                HardError("Unknown exception.");
            }
        }

        HardError("Terminated.");
    }

    void HardError(const std::string &message)
    {
        static bool first = true;
        if (!first)
            Exit(1);
        first = 0;

        Interface::MessageBox(Interface::MessageBoxType::error, "Error", "Error: " + message);
        Exit(1);
    }

    void SetErrorHandlers(bool replace_even_if_already_set)
    {
        bool just_set = false;

        auto SetHandlers = [&]
        {
            static constexpr int signal_enums[] {SIGSEGV, SIGABRT, SIGINT, SIGTERM, SIGFPE, SIGILL};
            for (int sig : signal_enums)
                std::signal(sig, SignalHandler);

            std::set_terminate(TerminateHandler);

            just_set = true;
            return nullptr;
        };

        [[maybe_unused]] static const std::nullptr_t once = SetHandlers();

        if (!just_set && replace_even_if_already_set)
            SetHandlers();
    }
}
