// PinballY Watchdog
//
// This is a child process that PinballY launches, to monitor
// the main process for crashes and to clean up any global
// system changes that the main process makes while running:
//
// - Taskbar window hiding:  the main process hides the Windows
//   desktop taskbar while a game is running, to avoid visual
//   clutter during transitions to and from the running game.
//   The watchdog ensures that we restore the taskbar to its
//   normal visible state if the main program exits abnormally
//   while the taskbar is hidden.
//
// This is invoked with the stdin and stdout handles set to
// anonymous pipes for communications between the two processes.
// The parent also passes us its process ID via the command line
// arguments so that we can monitor process termination.
//

#include "stdafx.h"
#include <string>
#include <list>
#include <unordered_map>
#include <regex>
#include "../Utilities/Util.h"
#include "../Utilities/WinUtil.h"
#include "../Utilities/StringUtil.h"
#include "resource.h"

// Taskbar state in the parent process
static bool taskbarHidden = false;

// Do our cleanup work on abnormal termination of the parent process
static void Cleanup()
{
	// if the parent left the taskbar hidden, show it
	if (taskbarHidden)
	{
		// hide/show all top-level windows with a given class name
		auto ShowTopLevelWindows = [](const TCHAR *className)
		{
			for (HWND hWnd = FindWindowEx(NULL, NULL, className, NULL);
				hWnd != NULL;
				hWnd = FindWindowEx(NULL, hWnd, className, NULL))
			{
				::ShowWindow(hWnd, SW_SHOW);
				::UpdateWindow(hWnd);
			}
		};

		// show/all taskbar and secondary taskbar windows, and "Button" 
		// windows for the Start button
		ShowTopLevelWindows(_T("Shell_TrayWnd"));
		ShowTopLevelWindows(_T("Shell_SecondaryTrayWnd"));
		ShowTopLevelWindows(_T("Button"));
	}
}

int main(int argc, char **argv)
{
	// parent process handle
	HandleHolder hProc;

	// parse arguments
	for (int i = 1; i < argc; ++i)
	{
		const char *arg = argv[i];
		std::match_results<const char*> m;
		if (std::regex_match(arg, m, std::regex("-pid=(\\d+)")))
		{
			// parent process ID
			DWORD ppid = (DWORD)atol(m[1].str().c_str());
			hProc = OpenProcess(SYNCHRONIZE, FALSE, ppid);
		}
		else
		{
			MessageBox(NULL, LoadStringT(IDS_ERR_USAGE), LoadStringT(IDS_ERR_CAPTION), MB_OK | MB_ICONERROR);
			return 2;
		}
	}

	// If we didn't get our parent process ID, this was probably a
	// standalone launch, so show an error and abort.
	if (hProc == NULL)
	{
		MessageBox(NULL, LoadStringT(IDS_ERR_STANDALONE), LoadStringT(IDS_ERR_CAPTION), MB_OK | MB_ICONINFORMATION);
		return 0;
	}

	// read requests from the pipe
	HANDLE readPipe = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE writePipe = GetStdHandle(STD_OUTPUT_HANDLE);
	for (int errcnt = 0; errcnt < 10; )
	{
		char buf[1024];
		DWORD actual;
		if (ReadFile(readPipe, buf, countof(buf), &actual, NULL))
		{
			// reset the consecutive error count on a successful read
			errcnt = 0;

			// see what's going on in the parent
			if (strcmp(buf, "Hide Taskbar") == 0)
			{
				taskbarHidden = true;
			}
			else if (strcmp(buf, "Restore Taskbar") == 0)
			{
				taskbarHidden = false;
			}
		}
		else
		{
			// Read failed.  If it's a broken pipe, the parent process
			// must have terminated abnormally, so do our cleanup work.
			++errcnt;
			if (GetLastError() == ERROR_BROKEN_PIPE)
			{
				Cleanup();
				break;
			}
		}
	}

	// done
    return 0;
}
