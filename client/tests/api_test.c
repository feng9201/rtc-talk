#include "talk/talk.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  if (!talk_version() || strcmp(talk_version(), "0.1.0") != 0) return 1;
  if (talk_create(NULL) || talk_command(NULL, "connect", "{}")) return 2;
  talk_options options = {sizeof(talk_options), TALK_ABI_VERSION, NULL, NULL, "{}"};
  options.abi_version++;
  if (talk_create(&options)) return 3;
  options.abi_version = TALK_ABI_VERSION;
  options.config_json = "[]";
  if (talk_create(&options)) return 4;
  options.config_json = "{\"audio_backend\":\"invalid\"}";
  if (talk_create(&options)) return 5;
  options.config_json = "{}";
  for (int i = 0; i < 100; ++i) {
    talk_handle* core = talk_create(&options);
    if (!core) return 6;
    if (talk_command(core, "connect", "null") || talk_command(core, "", "{}")) return 7;
    talk_destroy(core);
  }
  talk_destroy(NULL);
  puts("C ABI 与 100 次创建/销毁通过");
  return 0;
}
