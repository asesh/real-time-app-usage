//
//

#include <iostream>
#include <Windows.h>
#include <mutex>
#include <chrono>
#include <ctime>
#include <atlbase.h>
#include <Propkey.h>

std::string g_tracked_app_path;

std::mutex g_app_switch_mutex;

#include <atlbase.h>
#include <Propkey.h>

#pragma comment(lib, "version.lib")

// Source: https://stackoverflow.com/questions/53123914/retrieve-file-description-an-application-verqueryvalue
std::wstring get_app_description(const std::wstring& app_path) {
	DWORD length = GetFileVersionInfoSize(app_path.data(), nullptr);
	if (!length) {
		return L"";
	}

	std::unique_ptr<BYTE[]> version_info(new BYTE[length]);
	if (!GetFileVersionInfo(app_path.data(), 0, length, version_info.get())) {
		return L"";
	}

	static struct LANGANDCODEPAGE {
		WORD wLanguage;
		WORD wCodePage;
	} *translate;

	UINT translate_length = 0;
	if (!VerQueryValue(version_info.get(), L"\\VarFileInfo\\Translation",
		reinterpret_cast<void**>(&translate), &translate_length))
		return L"";

	for (unsigned int index = 0; index < (translate_length / sizeof(LANGANDCODEPAGE)); index++) {
		wchar_t sub_block[MAX_PATH * 2];
		//use sprintf if sprintf_s is not available
		wsprintf(sub_block, L"\\StringFileInfo\\%04x%04x\\FileDescription", translate[index].wLanguage, translate[index].wCodePage);
		wchar_t* app_description = nullptr;
		UINT buffer_length;
		if (VerQueryValue(version_info.get(), sub_block, reinterpret_cast<void**>(&app_description), &buffer_length)) {
			return app_description;
		}
	}
	return L"";
}

// Get the foreground app name
bool get_current_foreground_app_name(std::wstring& app_name) {

	wchar_t pe_file_path[250] = { 0 };
	DWORD buffer_size = sizeof(pe_file_path) / sizeof(pe_file_path[0]);

	DWORD foreground_app_process_id = 0;
	HWND window_handle = ::GetForegroundWindow();
	if (window_handle) {
		::GetWindowThreadProcessId(window_handle, &foreground_app_process_id);
		if (foreground_app_process_id) {
			HANDLE process_handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, foreground_app_process_id);
			if (process_handle) {
				::QueryFullProcessImageName(process_handle, 0, pe_file_path, &buffer_size);
				::CloseHandle(process_handle);
				app_name = pe_file_path;
				return true;
			}
		}
	}


	return false;
}


struct SProcessIds {
	DWORD old_process_id = 0;
	DWORD new_thread_process_id = 0;
	bool window_detected = false;
};

BOOL CALLBACK WindowEnumProc(HWND window_handle, LPARAM lParam) {
	SProcessIds* process_ids = reinterpret_cast<SProcessIds*>(lParam);
	DWORD enum_window_process_id;
	::GetWindowThreadProcessId(window_handle, &enum_window_process_id);
	if (enum_window_process_id && enum_window_process_id == process_ids->old_process_id && enum_window_process_id == process_ids->new_thread_process_id) {
		std::cout << "  Window enumerated and validation passed" << std::endl;
		//HANDLE process_handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, enum_window_process_id);
		//if (process_handle) {
		//	char image_path[MAX_PATH * 2] = { 0 };
		//	DWORD buffer_size = sizeof(image_path);

		//	// Get the buffer size required to hold full process image name
		//	// There's no need to check if it managed to get the absolute path of app as we have already zeroed out the buffer
		//	::QueryFullProcessImageNameA(process_handle, 0, image_path, &buffer_size);
		//	std::cout << "\n\nProcess path: " << image_path << std::endl;
		//	::CloseHandle(process_handle);
		//}
		process_ids->window_detected = true;
		return FALSE;
	}
	//std::cout << "    FAILED" << std::endl;
	process_ids->window_detected = false;
	return TRUE;
}

bool on_app_switched(HWND window_handle) {
	std::lock_guard<std::mutex> lock_mutex(g_app_switch_mutex);

	static std::time_t start_time;
	static uint64_t start_timestamp;

	DWORD process_id = 0;
	DWORD new_thread_process_id = 0;

	if (start_timestamp != 0) {
		std::cout << "\t\tEnding timestamp: " << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
	}

	DWORD thread_identifier = ::GetWindowThreadProcessId(window_handle, &process_id);
	if (process_id) {
		HANDLE process_handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
		if (process_handle) {
			char image_path[MAX_PATH * 2] = { 0 };
			DWORD buffer_size = sizeof(image_path);

			// Get the buffer size required to hold full process image name
			// There's no need to check if it managed to get the absolute path of app as we have already zeroed out the buffer
			::QueryFullProcessImageNameA(process_handle, 0, image_path, &buffer_size);

			// User switched to a new application
			if (g_tracked_app_path.compare(image_path) != 0) {
				//start_time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
				//auto end_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() - start_timestamp);

				// We were tracking another application before
				if (!g_tracked_app_path.empty()) {
					auto duration_milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() -
						start_timestamp;
					start_timestamp = 0;
					std::cout << "\t\tDuration elapsed: " << duration_milliseconds << " milliseconds -> " << duration_milliseconds / 1000.0f << " seconds" << std::endl;
				}
				std::wstring app_wide_description;
				std::string file_path(image_path);
				std::wstring image_wide_path(file_path.begin(), file_path.end());
				app_wide_description = get_app_description(image_wide_path);
				std::string app_description(app_wide_description.begin(), app_wide_description.end());

				GUITHREADINFO gui_thread_info = { 0 };
				gui_thread_info.cbSize = sizeof(GUITHREADINFO);
				::GetGUIThreadInfo(thread_identifier, &gui_thread_info);

				HWND foreground_window_handle = ::GetForegroundWindow();
				::GetWindowThreadProcessId(foreground_window_handle, &new_thread_process_id);

				std::cout << std::endl << "**Active app: " << image_path << "\n  App desc: " << app_description << "\n  Process id: " << process_id << std::endl
					<< "  Old Window handle visible: " << (::IsWindowVisible(window_handle) ? "True" : "False") << std::endl
					<< "  New Window handle visible: " << (::IsWindowVisible(foreground_window_handle) ? "True" : "False") << std::endl
					<< "  Same Window handle: " << (new_thread_process_id == process_id ? "True" : "False") << std::endl;
				SProcessIds process_ids = { process_id, new_thread_process_id };
				bool enumeration_successful = ::EnumWindows(WindowEnumProc, reinterpret_cast<LPARAM>(&process_ids));

				g_tracked_app_path = image_path;
				start_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
				std::cout << "\t\tStarting timestamp: " << start_timestamp << std::endl;
			}
			// else user didn't switch to a new application

			::CloseHandle(process_handle);

			return true;
		}
		else {
			std::cout << "**Unable to open process of process id: " << process_id << "**" << std::endl;
		}
	}
	return false;
}

LRESULT CALLBACK message_wnd_proc(HWND window_handle, UINT window_message, WPARAM wparam, LPARAM lparam) {
	switch (window_message) {
	case WM_CLOSE:
		::DestroyWindow(window_handle);
		break;

	case WM_DESTROY:
		::PostQuitMessage(0);
		break;

	case WM_KEYDOWN:
		switch (LOWORD(wparam)) {
		case VK_ESCAPE:
			::SendMessage(window_handle, WM_CLOSE, 0, 0);
			break;
		}

	default:
		return ::DefWindowProc(window_handle, window_message, wparam, lparam);
	}

	return 0;
}

void CALLBACK win_event_callback(
	HWINEVENTHOOK win_event_hook,
	DWORD window_event,
	HWND window_handle,
	LONG id_object,
	LONG id_child,
	DWORD event_thread,
	DWORD event_time) {
	switch (window_event) {
	case EVENT_SYSTEM_FOREGROUND:
		on_app_switched(window_handle);
		break;
	}
}

int main() {
	::SetConsoleTitle(L"CloudFactory Real-Time App Usage Info");
	::ShowWindow(::GetConsoleWindow(), SW_SHOWMAXIMIZED);

	on_app_switched(::GetConsoleWindow());

	static const wchar_t* class_name = L"app_usage";
	WNDCLASSEX wx = {};
	wx.cbSize = sizeof(WNDCLASSEX);
	wx.lpfnWndProc = message_wnd_proc;
	wx.hInstance = ::GetModuleHandle(L"");
	wx.lpszClassName = class_name;
	wx.style = CS_HREDRAW | CS_VREDRAW;
	if (RegisterClassEx(&wx)) {
		HWND message_window = CreateWindowEx(0, class_name, L"CloudFactory Real-time App Usage", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, HWND_MESSAGE, NULL, ::GetModuleHandle(L""), NULL);
		if (!message_window)
			return 1;
	}
	// Set windows event hook to monitor foreground window switches
	HWINEVENTHOOK window_event_hook = ::SetWinEventHook(
		EVENT_SYSTEM_FOREGROUND,
		EVENT_SYSTEM_FOREGROUND,
		nullptr,
		win_event_callback,
		0,
		0,
		WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
	MSG message = { 0 };
	while (::GetMessage(&message, nullptr, 0, 0)) {
		::DispatchMessage(&message);
	}
	::UnhookWinEvent(window_event_hook);
}
