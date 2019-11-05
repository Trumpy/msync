#include <catch2/catch.hpp>

#include "../lib/sync/send.hpp"
#include "../lib/net_interface/net_interface.hpp"
#include "../lib/queue/queues.hpp"
#include "../lib/options/global_options.hpp"
#include "../lib/constants/constants.hpp"

#include "test_helpers.hpp"

#include <string_view>
#include <vector>
#include <array>
#include <string>
#include <utility>
#include <algorithm>
#include <print_logger.hpp>

struct mock_args
{
	std::string url;
	std::string access_token;
	status_params params;
	attachment attachment_args;
	std::string id;
};

std::string make_status_json(const std::string& id)
{
	std::string toreturn = R"({"id": ")";
	toreturn += id;
	toreturn += R"(", "uri": "https://who.cares/api/statuses/123", "spoiler_text": "hey there", "content": "buddy guy", "visibility": "public"})";
	
	return toreturn;
}

struct mock_network
{
	int status_code = 200;
	bool fatal_error = false;

	void set_succeed_after(size_t n)
	{
		succeed_after = succeed_after_n = n;
	}

	std::vector<mock_args> arguments;
	
	net_response mock_post(std::string_view url, std::string_view access_token)
	{
		arguments.push_back(mock_args{ std::string {url}, std::string { access_token } });

		net_response toreturn;
		toreturn.retryable_error = (--succeed_after > 0);
		if (succeed_after == 0) { succeed_after = succeed_after_n; }
		toreturn.okay = !(fatal_error || toreturn.retryable_error);
		toreturn.status_code = status_code;
		if (!toreturn.okay)
			toreturn.message = R"({ "error": "some problem" })";
		return toreturn;
	}

	net_response mock_delete(std::string_view url, std::string_view access_token)
	{
		arguments.push_back(mock_args{ std::string {url}, std::string { access_token } });

		net_response toreturn;
		toreturn.retryable_error = (--succeed_after > 0);
		if (succeed_after == 0) { succeed_after = succeed_after_n; }
		toreturn.okay = !(fatal_error || toreturn.retryable_error);
		toreturn.status_code = status_code;
		if (!toreturn.okay)
			toreturn.message = R"({ "error": "some problem" })";
		return toreturn;
	}

	net_response mock_new_status(std::string_view url, std::string_view access_token, status_params params)
	{
		static unsigned int id = 1000000;
		std::string str_id = std::to_string(++id);
		arguments.push_back(mock_args{ std::string {url}, std::string { access_token }, std::move(params), {}, str_id});

		net_response toreturn;
		toreturn.retryable_error = (--succeed_after > 0);
		if (succeed_after == 0) { succeed_after = succeed_after_n; }
		toreturn.okay = !(fatal_error || toreturn.retryable_error);
		toreturn.status_code = status_code;
		if (!toreturn.okay)
			toreturn.message = R"({ "error": "some problem" })";
		else
			toreturn.message = make_status_json(str_id);
		return toreturn;
	}

	net_response mock_upload(std::string_view url, std::string_view access_token, const fs::path& file, std::string description)
	{
		static unsigned int id = 100;
		std::string str_id = std::to_string(++id);
		arguments.push_back(mock_args{ std::string {url}, std::string { access_token }, {},
			attachment{file, std::move(description)}, str_id });

		net_response toreturn;
		toreturn.retryable_error = (--succeed_after > 0);
		if (succeed_after == 0) { succeed_after = succeed_after_n; }
		toreturn.okay = !(fatal_error || toreturn.retryable_error);
		toreturn.status_code = status_code;
		toreturn.message = R"({"id": ")";
		toreturn.message += str_id;
		toreturn.message += "\"}";
		return toreturn;
	}

private:
	size_t succeed_after_n = 1;
	size_t succeed_after = succeed_after_n;
};

struct mock_network_post : public mock_network
{
	net_response operator()(std::string_view url, std::string_view access_token)
	{
		return mock_post(url, access_token);
	}
};

struct mock_network_delete : public mock_network
{
	net_response operator()(std::string_view url, std::string_view access_token)
	{
		return mock_delete(url, access_token);
	}
};

struct mock_network_new_status : public mock_network
{
	std::string fail_if_body;
	net_response operator()(std::string_view url, std::string_view access_token, status_params params)
	{
		if (!fail_if_body.empty())
			fatal_error = fail_if_body == params.body;
		return mock_new_status(url, access_token, std::move(params));
	}
};

struct mock_network_upload : public mock_network
{
	net_response operator()(std::string_view url, std::string_view access_token, const fs::path& file, std::string description)
	{
		return mock_upload(url, access_token, file, std::move(description));
	}
};

std::string make_expected_url(const std::string_view id, const std::string_view route, const std::string_view instance_url)
{
	std::string toreturn{ "https://" };
	toreturn.append(instance_url).append("/api/v1/statuses/").append(id).append(route);
	return toreturn;
}

std::vector<std::string_view> repeat_each_element(const std::vector<std::string>& in, size_t count)
{
	std::vector<std::string_view> toreturn;
	for (const auto& str : in)
	{
		for (size_t i = 0; i < count; i++)
		{
			toreturn.emplace_back(str);
		}
	}
	return toreturn;
}


SCENARIO("Send correctly sends from and modifies the queue with favs and boosts.")
{
	logs_off = true;

	const test_file fi = account_directory();
	constexpr std::string_view account = "someguy@cool.account";
	constexpr std::string_view instanceurl = "cool.account";
	constexpr std::string_view accesstoken = "sometoken";

	const auto queue = GENERATE(
		std::make_tuple(queues::fav, "/favourite", "/unfavourite"),
		std::make_tuple(queues::boost, "/reblog", "/unreblog"));

	const std::vector<std::string> testvect = GENERATE(
		std::vector<std::string>{ "someid", "someotherid", "mrid" },
		std::vector<std::string>{},
		std::vector<std::string>{ "justone" });

	const bool send_all = GENERATE(true, false);

	if (send_all)
	{
		auto& new_account = options().add_new_account(std::string{ account });
		new_account.second.set_option(user_option::instance_url, std::string{ instanceurl });
		new_account.second.set_option(user_option::access_token, std::string{ accesstoken });
	}


	GIVEN("A queue with some ids to add and a good connection")
	{
		enqueue(std::get<0>(queue), account, testvect);

		WHEN("the queue is sent")
		{
			mock_network_post mockpost;
			mock_network_delete mockdel;
			mock_network_new_status mocknew;
			mock_network_upload mockupload;

			auto send = send_posts{ mockpost, mockdel, mocknew, mockupload };

			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue is now empty.")
			{
				REQUIRE(print(std::get<0>(queue), account).empty());
			}

			THEN("one call per ID was made.")
			{
				REQUIRE(mockpost.arguments.size() == testvect.size());
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				REQUIRE(std::equal(mockpost.arguments.begin(), mockpost.arguments.end(), testvect.begin(), testvect.end(), [&](const auto& actual, const auto& expected)
					{
						return actual.url == make_expected_url(expected, std::get<1>(queue), instanceurl);
					}));

			}

			THEN("only the post function was called.")
			{
				REQUIRE(mockdel.arguments.empty());
				REQUIRE(mocknew.arguments.empty());
				REQUIRE(mockupload.arguments.empty());
			}
		}
	}

	GIVEN("A queue with some ids to remove and a good connection")
	{
		std::vector<std::string> toremove{ testvect };
		std::for_each(toremove.begin(), toremove.end(), [](auto& str) { str.push_back('-'); });

		enqueue(std::get<0>(queue), account, toremove);

		WHEN("the queue is sent")
		{
			mock_network_post mockpost;
			mock_network_delete mockdel;
			mock_network_new_status mocknew;
			mock_network_upload mockupload;

			auto send = send_posts{ mockpost, mockdel, mocknew, mockupload };

			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue is now empty.")
			{
				REQUIRE(print(std::get<0>(queue), account).empty());
			}

			THEN("one call per ID was made.")
			{
				REQUIRE(mockpost.arguments.size() == toremove.size());
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				//testvect doesn't have the trailing minus signs, which is what we want for this test.
				REQUIRE(std::equal(mockpost.arguments.begin(), mockpost.arguments.end(), testvect.begin(), testvect.end(), [&](const auto& actual, const auto& expected)
					{
						return actual.url == make_expected_url(expected, std::get<2>(queue), instanceurl);
					}));

			}

			THEN("only the post function was called.")
			{
				REQUIRE(mockdel.arguments.empty());
				REQUIRE(mocknew.arguments.empty());
				REQUIRE(mockupload.arguments.empty());
			}
		}
	}

	GIVEN("A queue with some ids to add and retryable errors that ultimately succeed")
	{
		// first is the number of retries to feed to send
		// second is the number of retries to expect
		// the last two test the "if retries less than 1, set to 3" behavior
		const auto retries = GENERATE(
			std::make_pair(3, 3),
			std::make_pair(5, 5),
			std::make_pair(1, 1),
			std::make_pair(0, 3),
			std::make_pair(-1, 3));

		enqueue(std::get<0>(queue), account, testvect);

		WHEN("the queue is sent")
		{
			mock_network_post mockpost;
			mockpost.set_succeed_after(retries.second);

			mock_network_delete mockdel;
			mock_network_new_status mocknew;
			mock_network_upload mockupload;

			auto send = send_posts{ mockpost, mockdel, mocknew, mockupload };

			send.retries = retries.first;

			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue is now empty.")
			{
				REQUIRE(print(std::get<0>(queue), account).empty());
			}

			THEN("each ID was tried the correct number of times.")
			{
				REQUIRE(mockpost.arguments.size() == testvect.size() * retries.second);
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				auto repeated = repeat_each_element(testvect, retries.second);
				REQUIRE(std::equal(mockpost.arguments.begin(), mockpost.arguments.end(), repeated.begin(), repeated.end(), [&](const auto& actual, const auto& expected)
					{
						return actual.url == make_expected_url(expected, std::get<1>(queue), instanceurl);
					}));

			}

			THEN("only the post function was called.")
			{
				REQUIRE(mockdel.arguments.empty());
				REQUIRE(mocknew.arguments.empty());
				REQUIRE(mockupload.arguments.empty());
			}
		}
	}

	GIVEN("A queue where all the IDs fail")
	{
		// first is the number of retries to feed to send
		// second is the number of retries to expect
		// the last two test the "if retries less than 1, set to 3" behavior
		const auto retries = GENERATE(
			std::make_pair(3, 3),
			std::make_pair(5, 5),
			std::make_pair(1, 1),
			std::make_pair(0, 3),
			std::make_pair(-1, 3));


		enqueue(std::get<0>(queue), account, testvect);

		WHEN("the queue is sent")
		{

			mock_network_post mockpost;
			mockpost.fatal_error = true;
			mockpost.status_code = 500;

			mock_network_delete mockdel;
			mock_network_new_status mocknew;
			mock_network_upload mockupload;

			auto send = send_posts{ mockpost, mockdel, mocknew, mockupload };

			send.retries = retries.first;

			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue hasn't changed.")
			{
				REQUIRE(print(std::get<0>(queue), account) == testvect);
			}

			THEN("each ID was tried once.")
			{
				REQUIRE(mockpost.arguments.size() == testvect.size());
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				REQUIRE(std::equal(mockpost.arguments.begin(), mockpost.arguments.end(), testvect.begin(), testvect.end(), [&](const auto& actual, const auto& expected)
					{
						return actual.url == make_expected_url(expected, std::get<1>(queue), instanceurl);
					}));

			}

			THEN("only the post function was called.")
			{
				REQUIRE(mockdel.arguments.empty());
				REQUIRE(mocknew.arguments.empty());
				REQUIRE(mockupload.arguments.empty());
			}
		}
	}
}

SCENARIO("Send correctly sends new posts and deletes existing ones.")
{
	logs_off = true;
	const test_file fi = account_directory();

	constexpr std::string_view account = "someguy@cool.account";
	constexpr std::string_view instanceurl = "cool.account";
	constexpr std::string_view accesstoken = "sometoken";
	constexpr std::string_view new_post_url = "https://cool.account/api/v1/statuses";

	const bool send_all = GENERATE(true, false);

	const static fs::path queue_directory = fi.filename / account / File_Queue_Directory;

	if (send_all)
	{
		auto& new_account = options().add_new_account(std::string{ account });
		new_account.second.set_option(user_option::instance_url, std::string{ instanceurl });
		new_account.second.set_option(user_option::access_token, std::string{ accesstoken });
	}

	GIVEN("A queue with some post filenames to send.")
	{
		const std::array<test_file, 4> to_enqueue { "first.post", "second.post", "another kind of post", "last one" };
		const std::array<touch_file, 4> attachment_files{ "attachments", "on", "this", "one" };

		const static std::vector<fs::path> expected_attach{ fs::canonical("attachments"), fs::canonical("on")
			, fs::canonical("this"), fs::canonical("one") };
		const static std::vector<std::string> expected_descriptions{ "with", "some", "descriptions", "" };
		const static std::vector<std::string> expected_files{ "first.post", "second.post", "another kind of post", "last one" };

		{
			outgoing_post first{ to_enqueue[0].filename };
			first.parsed.text = "This one just has a body.";
			first.parsed.reply_id = "Hi";

			outgoing_post second{ to_enqueue[1].filename };
			second.parsed.text = "This one has a body, too.";
			second.parsed.content_warning = "And a content warning.";
			second.parsed.vis = visibility::priv;
			second.parsed.reply_id = "hi2hi";
			second.parsed.reply_to_id = "Hi";

			outgoing_post third{ to_enqueue[2].filename };
			third.parsed.attachments = { "attachments", "on" };
			third.parsed.descriptions = { "with", "some", "descriptions" };
			third.parsed.reply_to_id = "hi2hi";
			third.parsed.vis = visibility::direct;

			outgoing_post fourth{ to_enqueue[3].filename };
			fourth.parsed.attachments = { "attachments", "on", "this", "one" };
			fourth.parsed.descriptions = { "with", "some", "descriptions" };
			fourth.parsed.reply_to_id = "777777";
			fourth.parsed.vis = visibility::unlisted;
		}

		enqueue(queues::post, account, expected_files);

		mock_network_post mockpost;
		mock_network_delete mockdel;
		mock_network_new_status mocknew;
		mock_network_upload mockupload;

		auto send = send_posts{ mockpost, mockdel, mocknew, mockupload };

		WHEN("the posts are sent over a good connection")
		{
			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue and post directory is now empty.")
			{
				REQUIRE(print(queues::post, account).empty());

				// it'll leave the .bak files behind
				REQUIRE(count_files_in_directory(queue_directory) == 4);
			}

			THEN("the input files and attachments are untouched")
			{
				REQUIRE(std::all_of(to_enqueue.begin(), to_enqueue.end(), [](const auto& file) { return fs::exists(file.filename); }));
				REQUIRE(std::all_of(attachment_files.begin(), attachment_files.end(), [](const auto& file) { return fs::exists(file.filename); }));
			}

			THEN("one call per ID was made.")
			{
				REQUIRE(mocknew.arguments.size() == 4);
			}

			THEN("the other APIs weren't called.")
			{
				REQUIRE(mockpost.arguments.empty());
				REQUIRE(mockdel.arguments.empty());
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mocknew.arguments.begin(), mocknew.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.url == new_post_url; }));
			}

			THEN("the post parameters are as expected.")
			{
				const auto& first = mocknew.arguments[0];
				REQUIRE(first.params.attachment_ids.empty());
				REQUIRE(first.params.body == "This one just has a body.");
				REQUIRE(first.params.content_warning.empty());
				REQUIRE(first.params.reply_to.empty());
				REQUIRE(first.params.visibility == "public");

				const auto& second = mocknew.arguments[1];
				REQUIRE(second.params.attachment_ids.empty());
				REQUIRE(second.params.body == "This one has a body, too.");
				REQUIRE(second.params.content_warning == "And a content warning.");
				REQUIRE(second.params.visibility == "private");
				REQUIRE(second.params.reply_to == first.id);

				const auto& third = mocknew.arguments[2];
				REQUIRE(third.params.attachment_ids.size() == 2);
				REQUIRE(third.params.body.empty());
				REQUIRE(third.params.content_warning.empty());
				REQUIRE(third.params.visibility == "direct");
				REQUIRE(third.params.reply_to == second.id);

				const auto& fourth = mocknew.arguments[3];
				REQUIRE(fourth.params.attachment_ids.size() == 4);
				REQUIRE(fourth.params.body.empty());
				REQUIRE(fourth.params.content_warning.empty());
				REQUIRE(fourth.params.reply_to == "777777");
				REQUIRE(fourth.params.visibility == "unlisted");
			}


			THEN("the uploads are as expected.")
			{
				REQUIRE(mockupload.arguments.size() == 6);
				// attached to the third 
				REQUIRE(mockupload.arguments[0].attachment_args.file == expected_attach[0]);
				REQUIRE(mockupload.arguments[0].attachment_args.description == expected_descriptions[0]);
				REQUIRE(mockupload.arguments[1].attachment_args.file == expected_attach[1]);
				REQUIRE(mockupload.arguments[1].attachment_args.description == expected_descriptions[1]);
				
				//attached to the fourth
				for (size_t i = 0; i < 4; i++)
				{
					REQUIRE(mockupload.arguments[i + 2].attachment_args.file == expected_attach[i]);
					REQUIRE(mockupload.arguments[i + 2].attachment_args.description == expected_descriptions[i]);
				}
			}

		}

		WHEN("one of the threaded posts fails to send")
		{
			mocknew.fail_if_body = "This one has a body, too.";

			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue and post directory removes the successfully sent posts.")
			{
				REQUIRE(print(queues::post, account) == std::vector<std::string>{ "second.post", "another kind of post" });

				// 4 bak files, 2 regular
				REQUIRE(count_files_in_directory(queue_directory) == 6);
			}

			THEN("the input files and attachments are untouched")
			{
				REQUIRE(std::all_of(to_enqueue.begin(), to_enqueue.end(), [](const auto& file) { return fs::exists(file.filename); }));
				REQUIRE(std::all_of(attachment_files.begin(), attachment_files.end(), [](const auto& file) { return fs::exists(file.filename); }));
			}

			THEN("one call per ID was made.")
			{
				// send first OK
				// try and fail to send second
				// don't try to send third
				// send fourth OK
				REQUIRE(mocknew.arguments.size() == 3);
			}

			THEN("the other APIs weren't called.")
			{
				REQUIRE(mockpost.arguments.empty());
				REQUIRE(mockdel.arguments.empty());
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mocknew.arguments.begin(), mocknew.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.url == new_post_url; }));
			}

			THEN("the post parameters are as expected.")
			{
				const auto& first = mocknew.arguments[0];
				REQUIRE(first.params.attachment_ids.empty());
				REQUIRE(first.params.body == "This one just has a body.");
				REQUIRE(first.params.content_warning.empty());
				REQUIRE(first.params.reply_to.empty());
				REQUIRE(first.params.visibility == "public");

				const auto& second = mocknew.arguments[1];
				REQUIRE(second.params.attachment_ids.empty());
				REQUIRE(second.params.body == "This one has a body, too.");
				REQUIRE(second.params.content_warning == "And a content warning.");
				REQUIRE(second.params.visibility == "private");
				REQUIRE(second.params.reply_to == first.id);

				// third should get skipped because second will fail to send

				const auto& fourth = mocknew.arguments[2];
				REQUIRE(fourth.params.attachment_ids.size() == 4);
				REQUIRE(fourth.params.body.empty());
				REQUIRE(fourth.params.content_warning.empty());
				REQUIRE(fourth.params.reply_to == "777777");
				REQUIRE(fourth.params.visibility == "unlisted");
			}


			THEN("the uploads are as expected.")
			{
				REQUIRE(mockupload.arguments.size() == 4);
				
				//attached to the fourth
				for (size_t i = 0; i < 4; i++)
				{
					REQUIRE(mockupload.arguments[i].attachment_args.file == expected_attach[i]);
					REQUIRE(mockupload.arguments[i].attachment_args.description == expected_descriptions[i]);
				}
			}

			THEN("all replies to a threaded post that fail to send persist the successful post's ID")
			{
				const auto& post_id = mocknew.arguments[0].id;
				readonly_outgoing_post failed_post{ queue_directory / "second.post" };
				REQUIRE(failed_post.parsed.reply_to_id == post_id);
			}

		}

		WHEN("the posts are sent over a bad or no connection")
		{
			mocknew.fatal_error = true;
			mocknew.status_code = 500;

			mockupload.fatal_error = true;
			mockupload.status_code = 500;

			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue and post directories are not empty.")
			{
				REQUIRE(print(queues::post, account) == expected_files);

				// queueing the posts makes a .bak file for them
				REQUIRE(count_files_in_directory(queue_directory) == 8);
			}

			THEN("the input files and attachments are untouched")
			{
				REQUIRE(std::all_of(to_enqueue.begin(), to_enqueue.end(), [](const auto& file) { return fs::exists(file.filename); }));
				REQUIRE(std::all_of(attachment_files.begin(), attachment_files.end(), [](const auto& file) { return fs::exists(file.filename); }));
			}

			THEN("one call per ID was made.")
			{
				REQUIRE(mocknew.arguments.size() == 1);
			}

			THEN("the other APIs weren't called.")
			{
				REQUIRE(mockpost.arguments.empty());
				REQUIRE(mockdel.arguments.empty());
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mocknew.arguments.begin(), mocknew.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.url == new_post_url; }));
			}

			THEN("the post parameters are as expected.")
			{
				const auto& first = mocknew.arguments[0];
				REQUIRE(first.params.attachment_ids.empty());
				REQUIRE(first.params.body == "This one just has a body.");
				REQUIRE(first.params.content_warning.empty());
				REQUIRE(first.params.visibility == "public");

				// the second call should never be made because it was a reply to the first.

				// the third and fourth calls should never be made because the uploads failed.

			}

			THEN("Only the first upload per post is attempted.")
			{
				REQUIRE(mockupload.arguments.size() == 1);
				REQUIRE(mockupload.arguments[0].attachment_args.file == expected_attach[0]);
				REQUIRE(mockupload.arguments[0].attachment_args.description == expected_descriptions[0]);
			}


		}

		WHEN("the posts are sent over a flaky connection that eventually succeeds")
		{
			const std::pair<int, size_t> retries = GENERATE(
				std::make_pair(3, 3),
				std::make_pair(5, 5),
				std::make_pair(1, 1),
				std::make_pair(0, 3),
				std::make_pair(-1, 3));

			mocknew.fatal_error = false;
			mocknew.set_succeed_after(retries.second);

			mockupload.fatal_error = false;
			mockupload.set_succeed_after(retries.second);

			send.retries = retries.first;

			if (send_all)
				send.send_all();
			else
				send.send(account, instanceurl, accesstoken);

			THEN("the queue and post directories are emptied.")
			{
				REQUIRE(print(queues::post, account).empty());

				// queueing the posts makes a .bak file for them
				REQUIRE(count_files_in_directory(queue_directory) == 4);
			}

			THEN("the input files and attachments are untouched")
			{
				REQUIRE(std::all_of(to_enqueue.begin(), to_enqueue.end(), [](const auto& file) { return fs::exists(file.filename); }));
				REQUIRE(std::all_of(attachment_files.begin(), attachment_files.end(), [](const auto& file) { return fs::exists(file.filename); }));
			}

			THEN("[retries] calls per ID was made.")
			{
				REQUIRE(mocknew.arguments.size() == 4 * retries.second);
			}

			THEN("the other APIs weren't called.")
			{
				REQUIRE(mockpost.arguments.empty());
				REQUIRE(mockdel.arguments.empty());
			}

			THEN("the access token was passed in.")
			{
				REQUIRE(std::all_of(mocknew.arguments.begin(), mocknew.arguments.end(), [&](const auto& actual) { return actual.access_token == accesstoken; }));
			}

			THEN("the URLs are as expected.")
			{
				REQUIRE(std::all_of(mockpost.arguments.begin(), mockpost.arguments.end(), [&](const auto& actual) { return actual.url == new_post_url; }));
			}

			THEN("the post parameters are as expected.")
			{
				size_t idx = 0;

				for (size_t i = 0; i < retries.second; i++)
				{
					const auto& first = mocknew.arguments[idx++];
					REQUIRE(first.params.attachment_ids.empty());
					REQUIRE(first.params.body == "This one just has a body.");
					REQUIRE(first.params.content_warning.empty());
					REQUIRE(first.params.reply_to.empty());
					REQUIRE(first.params.visibility == "public");
				}

				// the other replies will be to the last ID for this post
				// since that's the one that actually went through
				std::string reply_to_id = mocknew.arguments[idx - 1].id;

				for (size_t i = 0; i < retries.second; i++)
				{
					const auto& second = mocknew.arguments[idx++];
					REQUIRE(second.params.attachment_ids.empty());
					REQUIRE(second.params.body == "This one has a body, too.");
					REQUIRE(second.params.content_warning == "And a content warning.");
					REQUIRE(second.params.visibility == "private");

					// test that posts are threaded correctly
					REQUIRE(second.params.reply_to == reply_to_id);
				}

				// do it again because third is a reply to second
				reply_to_id = mocknew.arguments[idx - 1].id;

				for (size_t i = 0; i < retries.second; i++)
				{
					const auto& third = mocknew.arguments[idx++];
					REQUIRE(third.params.attachment_ids.size() == 2);
					REQUIRE(third.params.body.empty());
					REQUIRE(third.params.content_warning.empty());
					REQUIRE(third.params.visibility == "direct");
					REQUIRE(third.params.reply_to == reply_to_id);
				}

				for (size_t i = 0; i < retries.second; i++)
				{
					const auto& fourth = mocknew.arguments[idx++];
					REQUIRE(fourth.params.attachment_ids.size() == 4);
					REQUIRE(fourth.params.body.empty());
					REQUIRE(fourth.params.content_warning.empty());
					REQUIRE(fourth.params.reply_to == "777777");
					REQUIRE(fourth.params.visibility == "unlisted");
				}
			}

			THEN("the uploads are as expected.")
			{
				REQUIRE(mockupload.arguments.size() == 6 * retries.second);

				size_t idx = 0;

				// attached to the third 
				for (size_t i = 0; i < retries.second; i++)
				{
					REQUIRE(mockupload.arguments[idx].attachment_args.file == expected_attach[0]);
					REQUIRE(mockupload.arguments[idx].attachment_args.description == expected_descriptions[0]);
					idx++;
				}

				for (size_t i = 0; i < retries.second; i++)
				{
					REQUIRE(mockupload.arguments[idx].attachment_args.file == expected_attach[1]);
					REQUIRE(mockupload.arguments[idx].attachment_args.description == expected_descriptions[1]);
					idx++;
				}

				//attached to the fourth
				for (size_t i = 0; i < 4; i++)
				{
					for (size_t j = 0; j < retries.second; j++)
					{
						REQUIRE(mockupload.arguments[idx].attachment_args.file == expected_attach[i]);
						REQUIRE(mockupload.arguments[idx].attachment_args.description == expected_descriptions[i]);
						idx++;
					}
				}
			}
		}
	}
}
