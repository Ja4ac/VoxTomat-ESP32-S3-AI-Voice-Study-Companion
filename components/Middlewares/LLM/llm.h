#ifndef __LLM_H_
#define __LLM_H_

#include "esp_err.h"
#include "project_secrets.h"
#include <string.h>

#define LLM_BASE_URL          "https://api.deepseek.com/chat/completions"
#define LLM_API_KEY           PROJECT_LLM_API_KEY
#define LLM_SYSTEM_ROLE       "You are a helpful assistant."

esp_err_t llm_init();
esp_err_t llm_chat(const char *text_in, char **text_out);

#endif
