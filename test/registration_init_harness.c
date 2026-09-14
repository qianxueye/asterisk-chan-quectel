/* Framework stubs only: initialization, queue policy and response algorithms are extracted from production. */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>

#if defined(__clang__)
#if __has_warning("-Wunterminated-string-initialization")
/* Production AT command arrays deliberately omit the trailing NUL. */
#pragma clang diagnostic ignored "-Wunterminated-string-initialization"
#endif
#endif

#define ARRAY_LEN(value) (sizeof(value) / sizeof((value)[0]))
#define STRLEN(value) (sizeof(value) - 1)
#define ast_calloc calloc
#define ast_free free
#define ast_debug(...) ((void)0)
#define ast_log(...) ((void)0)
#define ast_verb(...) ((void)0)
#define CONF_SHARED(pvt, field) ((pvt)->field)
#define PVT_STATE(pvt, field) ((pvt)->field)
#define PVT_ID(pvt) "test-modem"
#define S_OR(value, fallback) ((value) ? (value) : (fallback))
#define ast_string_field_set(pvt, field, value) ((pvt)->field = (value))
#define AST_LIST_ENTRY(type) struct { struct type* next; }
#define AST_LIST_FIRST(head) ((head)->first)
#define AST_LIST_INSERT_TAIL(head, item, member) do { \
    (item)->member.next = NULL; \
    if ((head)->last) { (head)->last->member.next = (item); } else { (head)->first = (item); } \
    (head)->last = (item); \
} while (0)
#define AST_LIST_INSERT_AFTER(head, after, item, member) do { \
    (item)->member.next = (after)->member.next; (after)->member.next = (item); \
    if ((head)->last == (after)) { (head)->last = (item); } \
} while (0)
#define AST_LIST_REMOVE_HEAD(head, member) ({ \
    struct at_queue_task* removed = (head)->first; \
    if (removed) { (head)->first = removed->member.next; if (!(head)->first) { (head)->last = NULL; } } \
    removed; \
})

/* INSERT COMMAND TYPES */
typedef enum { RES_OK, RES_ERROR, RES_TIMEOUT } at_res_t;
struct pvt;
struct cpvt;
/* INSERT QUEUE TYPES */

struct cpvt { struct pvt* pvt; };
struct pvt {
    struct { struct at_queue_task* first; struct at_queue_task* last; } at_queue;
    struct cpvt sys_chan;
    unsigned int at_tasks;
    unsigned int at_cmds;
    unsigned int reset_modem;
    unsigned int dtmf;
    unsigned int dsci;
    long dtmf_duration;
    int msg_storage;
    int msg_direct;
    int msg_service;
    int call_waiting;
    int is_simcom;
    int has_voice;
    int gsm_registered;
    int gsm_reg_status;
    int act;
    const char* manufacturer;
    const char* location_area_code;
    const char* cell_id;
};
struct ast_str { char* data; };
#define ast_str_buffer(value) ((value)->data)
#define MESSAGE_STORAGE_AUTO 0
#define CALL_WAITING_AUTO 2
static const char MANUFACTURER_QUECTEL[] = "Quectel";
static const char MANUFACTURER_SIMCOM[] = "SimCom";
DECLARE_AT_CMD(at, "");

/* The transport does not access a serial port. Tests deliver responses below. */
int at_queue_run(struct pvt* pvt) { return 0; }
static const char* dc_msgstor2str(int storage) { return "SM"; }
static int at_fill_generic_cmd(at_queue_cmd_t* cmd, const char* format, ...)
{
    /* Only the existing DTMF duration command remains dynamic in these profiles. */
    assert(cmd->cmd == CMD_AT_VTD);
    va_list arguments;
    va_start(arguments, format);
    char buffer[64];
    int length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    assert(length > 0 && (unsigned int)length < sizeof(buffer));
    cmd->data = strdup(buffer);
    assert(cmd->data != NULL);
    cmd->length = (unsigned int)length;
    cmd->flags &= ~ATQ_CMD_FLAG_STATIC;
    return 0;
}
static int at_enqueue_cspn_cops(struct cpvt* cpvt) { return 0; }
static int at_enqueue_qspn_qnwinfo(struct cpvt* cpvt) { return 0; }
static int at_enqueue_set_ccwa(struct cpvt* cpvt, int mode) { assert(!"unexpected call waiting setting"); return -1; }
static void pvt_set_act(struct pvt* pvt, int act) { pvt->act = act; }
static int map_creg_act(int act) { return act; }
static char* ast_strip_quoted(char* text, const char* beginning, const char* ending)
{
    while (isspace((unsigned char)*text)) { ++text; }
    size_t length = strlen(text);
    while (length && isspace((unsigned char)text[length - 1])) { text[--length] = '\0'; }
    if (length >= 2 && *text == '"' && text[length - 1] == '"') { text[length - 1] = '\0'; ++text; }
    return text;
}

/* INSERT PRODUCTION FUNCTIONS */

static void run_initialization(const char* manufacturer, unsigned int dtmf, unsigned int reset_modem, int reject_lte_query)
{
    struct pvt pvt = {0};
    pvt.sys_chan.pvt = &pvt;
    pvt.msg_storage = MESSAGE_STORAGE_AUTO;
    pvt.msg_service = -1;
    pvt.call_waiting = CALL_WAITING_AUTO;
    pvt.gsm_reg_status = -1;
    pvt.dtmf = dtmf;
    pvt.reset_modem = reset_modem;

    assert(at_enqueue_initialization(&pvt.sys_chan) == 0);
    assert(pvt.at_tasks == 1);
    unsigned int index = 0;
    unsigned int creg_index = 0;
    unsigned int cereg_index = 0;
    unsigned int cereg_init_index = 0;
    unsigned int final_index = 0;
    unsigned int creg_count = 0;
    unsigned int cereg_count = 0;
    unsigned int final_count = 0;
    while (pvt.at_queue.first) {
        at_queue_task_t* const task = pvt.at_queue.first;
        const at_queue_cmd_t* const cmd = at_queue_task_cmd(task);
        at_res_t result = RES_OK;
        ++index;
        assert(index < 100);
        if (cmd->cmd == CMD_AT_CGMI) {
            struct ast_str response = {(char*)manufacturer};
            assert(at_response_cgmi(&pvt, &response) == 0);
            /* Manufacturer handling must append; it cannot replace the active common task. */
            assert(pvt.at_queue.first == task && pvt.at_tasks == 2);
            assert(task->entry.next != NULL);
        } else if (cmd->cmd == CMD_AT_CREG) {
            assert(cmd->length == strlen("AT+CREG?\r"));
            assert(!memcmp(cmd->data, "AT+CREG?\r", cmd->length));
            char text[] = "+CREG: 2,0";
            struct ast_str response = {text};
            assert(at_response_creg(&pvt, 0, &response) == 0);
            assert(pvt.gsm_registered == 0);
            creg_index = index;
            ++creg_count;
        } else if (cmd->cmd == CMD_AT_CEREG_INIT) {
            cereg_init_index = index;
        } else if (cmd->cmd == CMD_AT_CEREG) {
            assert(creg_count == 1 && creg_index < index);
            assert(cereg_init_index > creg_index && cereg_init_index < index);
            assert(cmd->length == strlen("AT+CEREG?\r"));
            assert(!memcmp(cmd->data, "AT+CEREG?\r", cmd->length));
            if (reject_lte_query) {
                result = RES_ERROR;
            } else {
                char text[] = "+CEREG: 2,1";
                struct ast_str response = {text};
                assert(at_response_creg(&pvt, 1, &response) == 0);
                assert(pvt.gsm_registered == 1 && pvt.gsm_reg_status == 1);
            }
            cereg_index = index;
            ++cereg_count;
        } else if (cmd->cmd == CMD_AT_FINAL) {
            final_index = index;
            ++final_count;
        }
        /* Advance the real queue on the terminal result, including optional query ERROR. */
        at_queue_remove_cmd(&pvt, result);
    }

    assert(creg_count == 1 && final_count == 1);
    assert(pvt.at_tasks == 0 && pvt.at_cmds == 0);
    if (!strcasecmp(manufacturer, "Quectel") || !strcasecmp(manufacturer, "SimCom")) {
        assert(cereg_count == 1 && creg_index < cereg_index && cereg_index < final_index);
        assert(pvt.gsm_registered == !reject_lte_query);
    } else {
        assert(cereg_count == 0 && pvt.gsm_registered == 0);
    }
}

int main(void)
{
    const char* manufacturers[] = {"Quectel", "SimCom", "Other"};
    for (unsigned int i = 0; i < ARRAY_LEN(manufacturers); ++i) {
        for (unsigned int dtmf = 0; dtmf <= 1; ++dtmf) {
            for (unsigned int reset = 0; reset <= 1; ++reset) {
                run_initialization(manufacturers[i], dtmf, reset, 0);
            }
        }
    }
    run_initialization("Quectel", 0, 0, 1);
    puts("Registration initialization passed: common CREG before vendor CEREG, observed LTE registration, optional query failure, 13 profiles");
    return 0;
}
