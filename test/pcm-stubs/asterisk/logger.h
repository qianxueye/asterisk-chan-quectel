#ifndef PCM_TEST_LOGGER_H
#define PCM_TEST_LOGGER_H
#define __LOG_ERROR 3
#define __LOG_DEBUG 7
#define AST_LOG_DEBUG __LOG_DEBUG
#define LOG_ERROR __LOG_ERROR, __FILE__, __LINE__, __func__
#define LOG_WARNING 4, __FILE__, __LINE__, __func__
#define DEBUG_ATLEAST(level) 0
static inline void pcm_test_log(int level, const char* file, int line, const char* function, const char* format, ...)
{
    (void)level;
    (void)file;
    (void)line;
    (void)function;
    (void)format;
}
#define ast_log pcm_test_log
#define ast_debug(level, ...) pcm_test_log(__LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#endif
