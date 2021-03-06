#include "account_directory.hpp"

#include <constants.hpp>

// basically, there's a number of paths we can take through here:

// if MSYNC_USER_CONFIG is set:
// - Windows has Windows-specific stuff
// - OSX and Linux share an implementation
// if MSYNC_USER_CONFIG is not set:
// - Linux has linux-specific stuff
// - Windows and OSX use whereami

#ifdef MSYNC_USER_CONFIG
	#ifdef _WIN32
		#define WIN32_LEAN_AND_MEAN
		#include <windows.h>
		#include <shlobj.h>
	#else
		#include <cstdlib>
	#endif
#else
	// realpath is available
	#if _BSD_SOURCE || _XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED
		#include <memory>
		#include <limits.h>
		#include <cstdlib>
	#else
		#include <whereami.h>
	#endif
#endif

#ifdef MSYNC_USER_CONFIG

fs::path get_user_config_folder_base()
{
#ifdef _WIN32
	PWSTR appdata_path = nullptr;
	auto hresult = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &appdata_path);
	if (SUCCEEDED(hresult)) {
		fs::path path{ appdata_path };
		::CoTaskMemFree(appdata_path);
		return fs::canonical(path);
	}
	return fs::path{};
#else // Linux and OSX
	// compliant with the XDG Base Directory Specification
	if (const char *config_dir = getenv("XDG_CONFIG_HOME")) {
		return config_dir;
	}
	return fs::path{ getenv("HOME") }.append(".config");
#endif
}

fs::path get_user_config_folder()
{
	return get_user_config_folder_base().append("msync");
}

#define get_config_folder get_user_config_folder

#else

fs::path get_executable_folder()
{
	// whereami doesn't work on arm processors for some reason, and also whereami uses PATH_MAX on Linux which you shouldn't do
	// (see https://linux.die.net/man/3/realpath, stackoverflow answers, and blogs that say you shouldn't use PATH_MAX)
	// so use the correct form of realpath on linux and whereami everywhere else
#if _BSD_SOURCE || _XOPEN_SOURCE >= 500 || _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED
	// this version of realpath malloc()s a buffer and returns it, so we use unique_ptr to free it automatically.
	std::unique_ptr<char[], decltype(std::free)*> full_executable_path { realpath("/proc/self/exe", NULL), std::free };
	fs::path to_return { full_executable_path.get() };
	to_return.remove_filename();
	return to_return;
#else //windows and maybe osx don't have realpath
	const int length = wai_getModulePath(nullptr, 0, nullptr);

	auto path = std::string((size_t)length + 1, '\0');

	int dirname_length;
	wai_getExecutablePath(&path[0], length, &dirname_length);
	return fs::path(path.begin(), path.begin() + dirname_length);
#endif
}

#define get_config_folder get_executable_folder

#endif

fs::path account_directory_path_uncached()
{
	return get_config_folder().append(Account_Directory);
}

const fs::path& account_directory_path()
{
	const static fs::path folder = account_directory_path_uncached();
	return folder;
}

