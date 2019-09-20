#include <iostream>

#include <exception>
#include <msync_exception.hpp>
#include <print_logger.hpp>
#include <string>

#include "../lib/options/global_options.hpp"
#include "../lib/options/option_enums.hpp"
#include "../lib/options/user_options.hpp"
#include "newaccount.hpp"
#include "optionparsing/parseoptions.hpp"

user_options& assume_account(user_options* user);
void print_stringptr(const std::string* toprint, print_logger<>& pl);

int main(int argc, const char* argv[])
{
    print_logger<logtype::fileonly> pl;
    print_logger<logtype::normal> plerr;
    pl << "--- msync started ---\n";
    pl.flush();

    auto parsed = parse(argc, argv, false);

    auto user = options.select_account(parsed.account);
    try
    {
        switch (parsed.selected)
        {
        case mode::newuser:
            make_new_account(parsed.account);
            break;
        case mode::showopt:
            print_stringptr(assume_account(user).get_option(parsed.toset), plerr);
            break;
        case mode::showallopt:
            for (user_option opt = user_option(0); opt <= user_option::pull_notifications; opt = user_option(static_cast<int>(opt) + 1))
            {
                plerr << USER_OPTION_NAMES[static_cast<int>(opt)] << ": ";
                if (opt < user_option::pull_home)
                    print_stringptr(assume_account(user).get_option(opt), plerr);
                else
                    plerr << SYNC_SETTING_NAMES[static_cast<int>(assume_account(user).get_sync_option(opt))];
                plerr << '\n';
            }
            plerr << "Accounts registered: ";
            for (auto it = options.accounts.begin(); it != options.accounts.end();)
            {
                plerr << it->first;
                if (++it != options.accounts.end())
                    plerr << ", ";
            }
            break;
        case mode::config:
            assume_account(user).set_option(parsed.toset, parsed.optionval);
            break;
		case mode::queue:
			switch (parsed.queue_opt.to_do)
			{

			}
        case mode::help:
            break;
        default:
            plerr << "[option not implemented]";
        }
    }
    catch (const std::exception& e)
    {
        plerr << "An error occurred: " << e.what();
        plerr << "\nFor account: " << parsed.account;
    }

    plerr << '\n';

    pl << "--- msync finished normally ---\n";
}

user_options& assume_account(user_options* user)
{
    if (user == nullptr)
        throw msync_exception("Could not find a match [or an unambiguous match].");
    return *user;
}

void print_stringptr(const std::string* toprint, print_logger<>& pl)
{
    if (toprint == nullptr)
        pl << "[not set]";
    else
        pl << *toprint;
}