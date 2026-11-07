#include "test.h"
#include "rtc_client/logging/logger.h"
#include <rtc_log_adapter.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>
using namespace rtc_client;
int main() {
  return test_main([] {
    auto path = std::filesystem::temp_directory_path() /
                ("rtc-rk-log-test-" + std::to_string(getpid()) + ".log");
    auto fields = logging::allowed_fields({{"token","SECRET"},{"password","SECRET"},
      {"sdp","v=0 SECRET"},{"generation",7},{"method","session.hello"},
      {"reason","abcdefgh.abcdefgh.abcdefgh"},{"state","Bearer SECRET"},
      {"code",403},{"nested",{{"secret","SECRET"}}}});
    CHECK(fields.size() == 3 && fields["generation"] == 7 && fields["code"] == 403);
    {
      logging::Runtime first(path.string());
      CHECK(first.healthy());
      {
        logging::Runtime second("/unused/log/path.log");
        CHECK(second.healthy());
        logging::emit(logging::Level::info,"session.ready",{{"generation",7},{"token","SECRET"}});
      }
      CHECK(first.healthy());
      logging::emit(logging::Level::warning,"session.failure",{{"code",403},{"sdp","SECRET"}});
    }  // last owner drains asynchronous log records.
    // Initialization in another DSO must not silence this module's formatter.
    const auto native_path=path.string();
    rtc_log_options shared{};
    shared.struct_size=sizeof(shared);shared.abi_version=RTC_LOG_ABI_VERSION;
    shared.file_path=native_path.c_str();shared.max_file_bytes=65536;
    shared.rotated_files=1;shared.queue_size=128;shared.level=RTC_LOG_INFO;
    CHECK(rtc_log_init(&shared)==RTC_LOG_OK);
    logging::emit(logging::Level::info,"media.shared_sdk_ready",{{"code",0}});
    rtc_log_shutdown();
    std::ifstream input(path);
    CHECK(input.good());
    std::ostringstream content; content << input.rdbuf();
    CHECK(content.str().find("session.ready") != std::string::npos);
    CHECK(content.str().find("session.failure") != std::string::npos);
    CHECK(content.str().find("media.shared_sdk_ready") != std::string::npos);
    CHECK(content.str().find("SECRET") == std::string::npos);
    CHECK(content.str().find("token") == std::string::npos);
    input.close();
    std::filesystem::remove(path);  // Only this test's generated log file.
  });
}
