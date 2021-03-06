#include "option_enums.hpp"

#include <msync_exception.hpp>

#include <string>
using namespace std::string_literals;

template <>
list_operations parse_enum<list_operations>(const char first)
{
	switch (first)
	{
	case 'a':
		return list_operations::add;
	case 'r':
		return list_operations::remove;
	case 'c':
		return list_operations::clear;
	}

	throw msync_exception("No list_operation starting with "s + first);
}

template <>
sync_settings parse_enum<sync_settings>(const char first)
{
	switch (first)
	{
	case 'd':
		return sync_settings::dont_sync;
	case 'n':
		return sync_settings::newest_first;
	case 'o':
		return sync_settings::oldest_first;
	}

	throw msync_exception("No sync_setting starting with "s + first);
}