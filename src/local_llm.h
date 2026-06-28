#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool local_llm_generate(const char *model_path,
                        const char *tokenizer_path,
                        const char *prompt,
                        int steps,
                        float temperature,
                        float topp,
                        char *output,
                        size_t output_size);

const char *local_llm_last_error(void);

#ifdef __cplusplus
}
#endif
