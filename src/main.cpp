// Copyright (c) 2017 Samsung Electronics Co., LTD
// Distributed under the MIT License.
// See the LICENSE file in the project root for more information.

#include <string>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef __linux__
#include <linux/limits.h>
#endif

#include "protocols/vscodeprotocol.h"
#include "debugger/manageddebugger.h"
#include "protocols/miprotocol.h"
#include "protocols/cliprotocol.h"
#include "utils/logger.h"
#include "version.h"

#ifdef _WIN32
#include <windows.h>
#define PATH_MAX MAX_PATH
static void setenv(const char* var, const char* val, int) { _putenv_s(var, val); }
#define getpid() (GetCurrentProcessId())
#endif

#ifdef __unix__
#include <sys/types.h>
#include <unistd.h>
#endif

#include "buildinfo.cpp"

namespace netcoredbg
{

static const uint16_t DEFAULT_SERVER_PORT = 4711;

static void print_help()
{
    fprintf(stdout,
        ".NET Core debugger\n"
        "\n"
        "Options:\n"
        "--buildinfo                           Print build info.\n"
        "--attach <process-id>                 Attach the debugger to the specified process id.\n"
        "--interpreter=cli                     Runs the debugger with Command Line Interface. \n"
        "--interpreter=mi                      Puts the debugger into MI mode.\n"
        "--interpreter=vscode                  Puts the debugger into VS Code Debugger mode.\n"
        "--engineLogging[=<path to log file>]  Enable logging to VsDbg-UI or file for the engine.\n"
        "                                      Only supported by the VsCode interpreter.\n"
        "--server[=port_num]                   Start the debugger listening for requests on the\n"
        "                                      specified TCP/IP port instead of stdin/out. If port is not specified\n"
        "                                      TCP %i will be used.\n"
        "--log[=<type>]                        Enable logging. Supported logging to file and to dlog (only for Tizen)\n"
        "                                      File log by default. File is created in 'current' folder.\n"
        "--version                             Displays the current version.\n",
        (int)DEFAULT_SERVER_PORT
    );
}

static void print_buildinfo()
{
    fprintf(stdout, ".NET Core debugger\n");

    fprintf(stdout,
        "\n"
        "    Build info:\n"
        "      Build date:  %s\n"
        "      Target OS:   %s\n"
        "      Target arch: %s\n"
        , BUILDINFO::BUILD_NETCOREDBG_DATE.c_str()
        , BUILDINFO::CMAKE_SYSTEM_NAME.c_str()
        , BUILDINFO::CLR_CMAKE_TARGET_ARCH.c_str()
    );

    fprintf(stdout,
        "\n"
        "    NetcoreDBG HEAD commit info:\n"
        "      Hash:    %s\n"
        "      Date:    %s\n"
        "      Subject: %s\n"
        "      Refs:    %s\n"
        , BUILDINFO::BUILD_NETCOREDBG_GIT_HEAD.c_str()
        , BUILDINFO::BUILD_NETCOREDBG_GIT_DATE.c_str()
        , BUILDINFO::BUILD_NETCOREDBG_GIT_SUBJECT.c_str()
        , BUILDINFO::BUILD_NETCOREDBG_GIT_REFSPEC.c_str()
    );

    fprintf(stdout,
        "\n"
        "    CoreCLR HEAD commit info:\n"
        "      Hash:    %s\n"
        "      Date:    %s\n"
        "      Subject: %s\n"
        "      Refs:    %s\n"
        "\n"
        , BUILDINFO::BUILD_CORECLR_GIT_HEAD.c_str()
        , BUILDINFO::BUILD_CORECLR_GIT_DATE.c_str()
        , BUILDINFO::BUILD_CORECLR_GIT_SUBJECT.c_str()
        , BUILDINFO::BUILD_CORECLR_GIT_REFSPEC.c_str()
    );
}

} // namespace netcoredbg

using namespace netcoredbg;

int main(int argc, char *argv[])
{
    DWORD pidDebuggee = 0;
    // prevent std::cout flush triggered by read operation on std::cin
    std::cin.tie(nullptr);

    enum InterpreterType
    {
        InterpreterMI,
        InterpreterVSCode,
        InterpreterCLI
    } interpreterType = InterpreterMI;

    bool engineLogging = false;
    std::string logFilePath;

    uint16_t serverPort = 0;

    std::string execFile;
    std::vector<std::string> execArgs;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--attach") == 0)
        {
            i++;
            if (i >= argc)
            {
                fprintf(stderr, "Error: Missing process id\n");
                return EXIT_FAILURE;
            }
            char *err;
            pidDebuggee = strtoul(argv[i], &err, 10);
            if (*err != 0)
            {
                fprintf(stderr, "Error: Missing process id\n");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[i], "--interpreter=mi") == 0)
        {
            interpreterType = InterpreterMI;
            continue;
        }
        else if (strcmp(argv[i], "--interpreter=vscode") == 0)
        {
            interpreterType = InterpreterVSCode;
            continue;
        }
        else if (strcmp(argv[i], "--interpreter=cli") == 0)
        {
            interpreterType = InterpreterCLI;
            continue;
        }
        else if (strcmp(argv[i], "--engineLogging") == 0)
        {
            engineLogging = true;
            continue;
        }
        else if (strstr(argv[i], "--engineLogging=") == argv[i])
        {
            engineLogging = true;
            logFilePath = argv[i] + strlen("--engineLogging=");
            continue;
        }
        else if (strcmp(argv[i], "--help") == 0)
        {
            print_help();
            return EXIT_SUCCESS;
        }
        else if (strcmp(argv[i], "--buildinfo") == 0)
        {
            print_buildinfo();
            return EXIT_SUCCESS;
        }
        else if (strcmp(argv[i], "--version") == 0)
        {
            fprintf(stdout, "NET Core debugger %s\n", __VERSION);
            fprintf(stdout, "\nCopyright (c) 2020 Samsung Electronics Co., LTD\n");
            fprintf(stdout, "Distributed under the MIT License.\n");
            fprintf(stdout, "See the LICENSE file in the project root for more information.\n");
            return EXIT_SUCCESS;
        }
        else if (strcmp(argv[i], "--log") == 0)
        {
            #ifdef _WIN32
            static const char path_separator[] = "/\\";
            #else
            static const char path_separator[] = "/";
            #endif

            // somethat similar to basename(3)
            char *s = argv[0] + strlen(argv[0]);
            while (s > argv[0] && !strchr(path_separator, s[-1])) s--;

            char tmp[PATH_MAX];
            snprintf(tmp, sizeof(tmp), "%s/%s.%u.log", GetTempFolder().c_str(), s, getpid());
            setenv("LOG_OUTPUT", tmp, 1);
        }
        else if (strstr(argv[i], "--log=") == argv[i])
        {
            setenv("LOG_OUTPUT", argv[i] + strlen("--log="), 1);
        }
        else if (strcmp(argv[i], "--server") == 0)
        {
            serverPort = DEFAULT_SERVER_PORT;
            continue;
        }
        else if (strstr(argv[i], "--server=") == argv[i])
        {
            char *err;
            serverPort = static_cast<uint16_t>(strtoul(argv[i] + strlen("--server="), &err, 10));
            if (*err != 0)
            {
                fprintf(stderr, "Error: Missing process id\n");
                return EXIT_FAILURE;
            }
            continue;
        }
        else if (strcmp(argv[i], "--") == 0)
        {
            ++i;
            if (i < argc)
            {
                execFile = argv[i];
            }
            else
            {
                fprintf(stderr, "Error: Missing program argument\n");
                return EXIT_FAILURE;
            }
            for (++i; i < argc; ++i)
            {
                execArgs.push_back(argv[i]);
            }
            break;
        }
        else
        {
            fprintf(stderr, "Error: Unknown option %s\n", argv[i]);
            return EXIT_FAILURE;
        }
    }

    LOGI("Start logging");

    ManagedDebugger debugger;
    std::unique_ptr<Protocol> protocol;

    switch(interpreterType)
    {
        case InterpreterMI:
        {
            LOGI("InterpreterMI selected");
            if (engineLogging)
            {
                fprintf(stderr, "Error: Engine logging is only supported in VsCode interpreter mode.\n");
                LOGI("Error: Engine logging is only supported in VsCode interpreter mode.");
                return EXIT_FAILURE;
            }
            MIProtocol *miProtocol = new MIProtocol();
            protocol.reset(miProtocol);
            miProtocol->SetDebugger(&debugger);
            LOGI("SetDebugger for InterpreterMI");
            if (!execFile.empty())
                miProtocol->SetLaunchCommand(execFile, execArgs);
            break;
        }
        case InterpreterVSCode:
        {
            LOGI("InterpreterVSCode selected");
            VSCodeProtocol *vsCodeProtocol = new VSCodeProtocol();
            protocol.reset(vsCodeProtocol);
            vsCodeProtocol->SetDebugger(&debugger);
            LOGI("SetDebugger for InterpreterVSCode");
            if (engineLogging)
                vsCodeProtocol->EngineLogging(logFilePath);
            if (!execFile.empty())
                vsCodeProtocol->OverrideLaunchCommand(execFile, execArgs);
            break;
        }
        case InterpreterCLI:
        {
            LOGI("InterpreterCLI selected");
            if (engineLogging)
            {
                fprintf(stderr, "Error: Engine logging is only supported in VsCode interpreter mode.\n");
                LOGI("Error: Engine logging is only supported in VsCode interpreter mode.");
                return EXIT_FAILURE;
            }
            CLIProtocol *cliProtocol = new CLIProtocol();
            protocol.reset(cliProtocol);
            cliProtocol->SetDebugger(&debugger);
            LOGI("SetDebugger for InterpreterCLI");
            if (!execFile.empty())
                cliProtocol->SetLaunchCommand(execFile, execArgs);
            break;
        }
    }

    debugger.SetProtocol(protocol.get());

    std::unique_ptr<IORedirectServer> server;
    if (interpreterType != InterpreterCLI)
    {
        server.reset(new IORedirectServer (
            serverPort,
            [&protocol](const std::string& text) { protocol->EmitOutputEvent(OutputEvent(OutputStdOut, text)); },
            [&protocol](const std::string& text) { protocol->EmitOutputEvent(OutputEvent(OutputStdOut, text)); }
        ));
    }

    LOGI("pidDebugee %d", pidDebuggee);
    if (pidDebuggee != 0)
    {
        debugger.Initialize();
        debugger.Attach(pidDebuggee);
        HRESULT Status = debugger.ConfigurationDone();
        if (FAILED(Status))
        {
            fprintf(stderr, "Error: 0x%x Failed to attach to %i\n", Status, pidDebuggee);
            return EXIT_FAILURE;
        }
    }

    protocol->CommandLoop();
    return EXIT_SUCCESS;
}