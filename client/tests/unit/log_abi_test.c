#define _POSIX_C_SOURCE 200809L
#include <rtc_log_adapter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"C ABI check failed at line %d\n",__LINE__); return 1; } } while (0)

int main(void) {
  char file[] = "/tmp/rtc-log-abi-XXXXXX";
  const int fd = mkstemp(file);
  REQUIRE(fd >= 0);
  REQUIRE(close(fd) == 0);
  REQUIRE(rtc_log_abi_version() == RTC_LOG_ABI_VERSION);
  REQUIRE(rtc_log_is_initialized() == 0);
  REQUIRE(rtc_log_init(NULL) == RTC_LOG_INVALID);
  REQUIRE(rtc_log_write(RTC_LOG_INFO,"record",6) == RTC_LOG_UNAVAILABLE);
  rtc_log_options config = {0};
  config.struct_size = sizeof(config);
  config.abi_version = 99;
  config.file_path = file;
  config.max_file_bytes = 65536;
  config.rotated_files = 1;
  config.queue_size = 128;
  config.level = RTC_LOG_INFO;
  REQUIRE(rtc_log_init(&config) == RTC_LOG_INVALID);
  config.abi_version = RTC_LOG_ABI_VERSION;
  REQUIRE(rtc_log_init(&config) == RTC_LOG_OK);
  REQUIRE(rtc_log_is_initialized() == 1);
  REQUIRE(rtc_log_init(&config) == RTC_LOG_CONFLICT);
  REQUIRE(rtc_log_write(99,"record",6) == RTC_LOG_INVALID);
  REQUIRE(rtc_log_write(RTC_LOG_INFO,NULL,6) == RTC_LOG_INVALID);
  REQUIRE(rtc_log_write(RTC_LOG_INFO,"record",4097) == RTC_LOG_INVALID);
  REQUIRE(rtc_log_write(RTC_LOG_INFO,"c-abi-record",12) == RTC_LOG_OK);
  rtc_log_shutdown();
  rtc_log_shutdown();
  REQUIRE(rtc_log_is_initialized() == 0);
  FILE* input = fopen(file,"r");
  REQUIRE(input != NULL);
  char content[4096] = {0};
  const size_t bytes = fread(content,1,sizeof(content)-1,input);
  REQUIRE(bytes > 0 && strstr(content,"c-abi-record") != NULL);
  REQUIRE(fclose(input) == 0);
  REQUIRE(unlink(file) == 0);
  puts("PASS");
  return 0;
}
