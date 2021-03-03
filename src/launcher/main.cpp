﻿#define CURL_STATICLIB
#define _CRT_SECURE_NO_WARNINGS

#include <Windows.h>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <future>
#include <vector>
#include <nlohmann/json.hpp>
#include <common/common.h>
#include "sciter-x.h"
#include "sciter-x-window.hpp"
#include "resources.cpp"

namespace fs = std::filesystem;

const char* LAUNCHER_BUCKET = "https://storage.googleapis.com/storage/v1/b/pd2-beta-launcher-update/o";
const char* CLIENT_FILES_BUCKET = "https://storage.googleapis.com/storage/v1/b/pd2-beta-client-files/o";

std::vector<std::string> dont_update = { "D2.LNG", "BnetLog.txt", "ProjectDiablo.cfg", "ddraw.ini", "default.filter", "loot.filter", "UI.ini" };
HANDLE pd2Mutex;

void updateLauncher() {
	nlohmann::json json = callJsonAPI(LAUNCHER_BUCKET);

	for (auto& element : json["items"]) {
		std::string itemName = element["name"];
		std::string mediaLink = element["mediaLink"];
		std::string crcHash = element["crc32c"];

		// get the absolute path to the file/item in question
		fs::path path = fs::current_path();
		path = path / itemName;
		path = path.lexically_normal();

		if (itemName == "PD2Launcher.exe" && !compareCRC(path, crcHash)) {
			MessageBox(NULL, L"An update for the launcher was found and will be downloaded now.", L"Update ready!", MB_OK | MB_ICONINFORMATION);

			// close the mutex
			if (pd2Mutex) {
				ReleaseMutex(pd2Mutex);
				CloseHandle(pd2Mutex);
			}

			// launch the updater
			STARTUPINFO info = { sizeof(info) };
			PROCESS_INFORMATION processInfo;
			std::wstring commandLine = L"updater.exe /f /l";
			if (CreateProcess(NULL, &commandLine[0], NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo)) {
				CloseHandle(processInfo.hProcess);
				CloseHandle(processInfo.hThread);

				// exit the launcher
				exit(0);
			}
		}
	}
}

void updateClientFiles() {
	nlohmann::json json = callJsonAPI(CLIENT_FILES_BUCKET);

	// loop through items
	for (auto& element : json["items"]) {
		std::string itemName = element["name"];
		std::string mediaLink = element["mediaLink"];
		std::string crcHash = element["crc32c"];

		// if the itemName ends in a slash, its not a file
		if (hasEnding(itemName, "/")) {
			continue;
		}

		// get the absolute path to the file/item in question
		fs::path path = fs::current_path();
		path = path / itemName;
		path = path.lexically_normal();

		// create any needed directories
		fs::create_directories(path.parent_path());

		// check if it doesnt exist or the crc32c hash doesnt match
		if (!fs::exists(path)) {
			downloadFile(mediaLink, path.string());
		}
		else if (!compareCRC(path, crcHash)) {
			// Don't update certain files (config files, etc.)
			if (std::find(dont_update.begin(), dont_update.end(), itemName) == dont_update.end()) {
				downloadFile(mediaLink, path.string());
			}
		}
	}
}

class frame : public sciter::window {
public:
	frame() : window(SW_MAIN) {}

	// passport - lists native functions and properties exposed to script:
	SOM_PASSPORT_BEGIN(frame)
		SOM_FUNCS(
			SOM_FUNC(play)
		)
		SOM_PASSPORT_END

		bool _update(sciter::string args) {
		updateLauncher();
		updateClientFiles();

		this->call_function("self.finish_update");
		return _launch(args);
	}

	bool _launch(sciter::string args) {
		STARTUPINFO info = { sizeof(info) };
		PROCESS_INFORMATION processInfo;
		std::wstring commandLine = L"Diablo II.exe " + args;
		if (CreateProcess(NULL, &commandLine[0], NULL, NULL, TRUE, 0, NULL, NULL, &info, &processInfo)) {
			//WaitForSingleObject(processInfo.hProcess, INFINITE);
			CloseHandle(processInfo.hProcess);
			CloseHandle(processInfo.hThread);

			return true;
		}

		return false;
	}

	bool play(sciter::string args) {
		auto fut = std::async(std::launch::async, &frame::_update, this, args);
		pending_futures.push_back(std::move(fut));

		return true;
	}

private:
	std::vector<std::future<bool>> pending_futures;
};

int uimain(std::function<int()> run) {
	// TODO: Check for sciter.dll

	// ensure only one running instance
	pd2Mutex = CreateMutex(NULL, TRUE, L"pd2.launcher.mutex");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		MessageBox(NULL, L"The Project Diablo 2 Launcher is already running! Please close it before running it again.", L"Already running!", MB_OK | MB_ICONERROR);
		return 0;
	}

	// enable debug mode
	//SciterSetOption(NULL, SCITER_SET_DEBUG_MODE, TRUE);

	// needed for persistant storage
	SciterSetOption(NULL, SCITER_SET_SCRIPT_RUNTIME_FEATURES, ALLOW_FILE_IO | ALLOW_SOCKET_IO | ALLOW_EVAL | ALLOW_SYSINFO);

	sciter::archive::instance().open(aux::elements_of(resources)); // bind resources[] (defined in "resources.cpp") with the archive
	sciter::om::hasset<frame> pwin = new frame();

	// note: this:://app URL is dedicated to the sciter::archive content associated with the application
	pwin->load(WSTR("this://app/main.htm"));
	pwin->expand();

	// start the launcher ui
	int result = run();

	// close the mutex
	if (pd2Mutex) {
		ReleaseMutex(pd2Mutex);
		CloseHandle(pd2Mutex);
	}

	return result;
}
