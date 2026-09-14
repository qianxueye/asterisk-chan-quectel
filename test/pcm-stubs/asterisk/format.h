#ifndef PCM_TEST_FORMAT_H
#define PCM_TEST_FORMAT_H
struct ast_format {
    unsigned int sample_rate;
    unsigned int default_ms;
};
static inline unsigned int ast_format_get_sample_rate(const struct ast_format* fmt) { return fmt->sample_rate; }
static inline unsigned int ast_format_get_default_ms(const struct ast_format* fmt) { return fmt->default_ms; }
#endif
