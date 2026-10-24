#!/usr/bin/env python3
"""Execute the real log functions and incoming SQL against synthetic payloads."""
import ast
import os
from pathlib import Path
import re
import sqlite3
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('SMS_PRIVACY_SOURCE', Path(__file__).resolve().parents[1]))


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    level = 1
    position = opening + 1
    while level:
        if source[position] == '{':
            level += 1
        elif source[position] == '}':
            level -= 1
        position += 1
    return source[start:position]


def sql(name):
    source = (ROOT/'src/smsdb.c').read_text()
    match = re.search(r'DEFINE_(?:INTERNAL_)?SQL_STATEMENT\('+name+r',\s*((?:"(?:[^"\\]|\\.)*"\s*)+)\)', source)
    return ''.join(ast.literal_eval(value) for value in re.findall(r'"(?:[^"\\]|\\.)*"', match.group(1)))


class PrivacyTests(unittest.TestCase):
    def test_valid_delivery_report_ack_does_not_depend_on_database_match(self):
        source = (ROOT/'src/at_response.c').read_text()
        status = function(source, 'case PDUTYPE_MTI_SMS_STATUS_REPORT:')
        ack = function(source, 'msg_done_ack:')
        code = '''#include <assert.h>
#include <stdlib.h>
struct pvt {int msg_service; const char *imsi; int sys_chan;};
struct ast_str {const char *value;}; struct ast_json;
enum {PDUTYPE_MTI_SMS_STATUS_REPORT=1, RES_CMGR, RES_CMT, RES_CDS, RES_CLASS0};
enum {TRIBOOL_FALSE=-1, TRIBOOL_NONE=0, TRIBOOL_TRUE=1};
static int database_result, acknowledgements, aggregates;
#define CONF_SHARED(p,f) ((p)->f)
#define PVT_ID(p) "quectel1"
#define ast_str_buffer(s) ((s)->value)
#define RAII_VAR(type,name,value,cleanup) type name=value
#define ast_calloc calloc
#define ast_verb(...) ((void)0)
#define ast_log(...) ((void)0)
#define ast_json_object_create() ((struct ast_json *)0)
#define ast_json_array_create() ((struct ast_json *)0)
#define ast_json_object_set(...) ((void)0)
#define ast_json_array_append(...) ((void)0)
#define channel_start_local_report(...) (++aggregates)
#define at_sms_retrieved(...) ((void)0)
#define at_enqueue_msg_ack(...) ((void)0)
static int smsdb_outgoing_part_status(const char *id,const char *number,int mr,int st,int *statuses)
{statuses[0]=-1;return database_result;}
static int at_enqueue_msg_ack_n(int *channel,int n,int uid)
{assert(n==0 && uid==45);++acknowledgements;return 0;}
static void report(int service,int result) {
struct pvt state={service,"synthetic"}; struct pvt *pvt=&state;
struct ast_str number={"+10000000000"}; struct ast_str *oa=&number;
int mr=45,st=0,msg_ack=TRIBOOL_NONE,msg_ack_uid=0,msg_complete=0,cmd=RES_CDS;
database_result=result;acknowledgements=0;aggregates=0;
switch(PDUTYPE_MTI_SMS_STATUS_REPORT) {
'''+status+'\n}\n'+ack+'''
assert(acknowledgements==(service>0));
assert(aggregates==(result==0));
}
int main(void) {
for(int service=-1;service<=1;service++) {
report(service,0);report(service,1);report(service,-1);
}
return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'test.c').write_text(code)
            subprocess.run(['cc', '-std=c11', str(path/'test.c'), '-o', str(path/'test')],
                           check=True, capture_output=True)
            subprocess.run([str(path/'test')], check=True, capture_output=True)

    def test_delivery_receipts_remain_visible_with_verbose_disabled(self):
        source = (ROOT/'src/at_response.c').read_text()
        blocks = [function(source, 'static int at_response_cmgs('),
                  function(source, 'case PDUTYPE_MTI_SMS_STATUS_REPORT:')]
        statements = [statement for block in blocks
                      for statement in re.findall(r'ast_(?:verb|debug|log)\([^;]+;', block)
                      if 'Successfully sent message' in statement or 'Got status report' in statement]
        self.assertEqual(len(statements), 4)
        code = '''#include <stdio.h>
struct ast_str {const char *value;};
struct task {int uid;};
#define PVT_ID(p) "quectel1"
#define LOG_NOTICE 1
#define ast_verb(level, ...) do {if ((level) <= 0) printf(__VA_ARGS__);} while (0)
#define ast_debug(level, ...) do {} while (0)
#define ast_log(level, ...) do {if ((level) == LOG_NOTICE) printf(__VA_ARGS__);} while (0)
const char *ast_str_buffer(const struct ast_str *s) {return s->value;}
int main(void) {
struct task value={123}; const struct task *task=&value;
int refid=45, partcnt=3, partno=1, mr=45, st=0;
struct ast_str number={"+10000000000"}; const struct ast_str *oa=&number;
'''+ '\n'.join(statements) + '\nreturn 0;}\n'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'test.c').write_text(code)
            subprocess.run(['cc', '-std=c11', str(path/'test.c'), '-o', str(path/'test')],
                           check=True, capture_output=True)
            output = subprocess.check_output([str(path/'test')], text=True)
        self.assertEqual(output.splitlines(), [
            '[quectel1][SMS:123 REF:45] Successfully sent message',
            '[quectel1][SMS:123] Successfully sent message [3 parts]',
            '[quectel1][SMS:45] Got status report from +10000000000 and status code 0',
        ])

    def test_actual_sms_and_channel_variable_logs_omit_body(self):
        source = (ROOT/'src/at_response.c').read_text()
        code = '''#include <stdio.h>
#include <stdarg.h>
#include <string.h>
struct pvt {int dummy;}; struct ast_channel {int dummy;};
struct ast_str {const char *value;}; typedef struct {int cmd;} at_queue_cmd_t;
typedef int at_res_t;
enum {CMD_USER=1, RES_OK, RES_ERROR, RES_SMS_PROMPT, RES_CMGR, RES_CMGL, RES_CMT, RES_CBM, RES_CDS, RES_CLASS0};
#define PVT_ID(p) "quectel0"
#define S_OR(v,f) ((v)?(v):(f))
#define ast_strlen_zero(v) (!(v) || !*(v))
#define ast_debug(level, ...) printf(__VA_ARGS__)
#define ast_verb(level, ...) printf(__VA_ARGS__)
const char *ast_str_buffer(const struct ast_str *s) {return s->value;}
const char *tmp_esc_str(const struct ast_str *s) {return s->value;}
const char *at_cmd2str(int n) {return "USER";}
const char *at_res2str(int n) {return "SMS";}
int pbx_builtin_setvar_helper(struct ast_channel *c, const char *n, const char *v) {return 0;}
'''
        code += function(source, 'static int check_at_res(')
        code += function(source, 'static void show_response(')
        code += function((ROOT/'src/channel.c').read_text(), 'static int setvar_helper(')
        code += '''int main(void) {
struct pvt p={0}; struct ast_channel ch={0}; struct ast_str s={"CANARY_PRIVATE_BODY"};
at_queue_cmd_t cmd={CMD_USER};
for (int n=RES_CMGR;n<=RES_CLASS0;n++) {show_response(&p,&cmd,&s,n);show_response(&p,NULL,&s,n);}
setvar_helper(&p,&ch,"SMS","CANARY_PRIVATE_BODY"); return 0;}
'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'test.c').write_text(code)
            subprocess.run(['cc', '-std=c11', str(path/'test.c'), '-o', str(path/'test')], check=True, capture_output=True)
            output = subprocess.check_output([str(path/'test')], text=True)
        self.assertIn('SMS', output)
        self.assertNotIn('CANARY_PRIVATE_BODY', output)

    def test_incoming_parts_never_enter_disk_database_or_backup(self):
        with tempfile.TemporaryDirectory() as directory:
            filename = Path(directory)/'sms.sqlite'
            db = sqlite3.connect(filename)
            db.execute('PRAGMA temp_store=MEMORY')
            db.execute(sql('create_incomingmsg'))
            db.execute(sql('create_incomingmsg_index'))
            db.execute(sql('put_incomingmsg'), ('key', 1, 600, 'CANARY_PRIVATE_BODY'))
            self.assertEqual(db.execute(sql('get_incomingmsg_cnt'), ('key',)).fetchone()[0], 1)
            self.assertEqual(db.execute('SELECT name FROM main.sqlite_master').fetchall(), [])
            self.assertEqual(db.execute('PRAGMA temp_store').fetchone()[0], 2)
            db.commit()
            backup = Path(directory)/'backup.sqlite'
            db.execute('VACUUM INTO ?', (str(backup),))
            db.close()
            for file in (filename, backup):
                self.assertNotIn(b'CANARY_PRIVATE_BODY', file.read_bytes())

    def test_incoming_expiry_and_capacity_query(self):
        db = sqlite3.connect(':memory:')
        self.addCleanup(db.close)
        db.execute(sql('create_incomingmsg'))
        db.execute(sql('put_incomingmsg'), ('old', 1, -1, 'old body'))
        db.execute(sql('put_incomingmsg'), ('new', 1, 600, 'fresh body'))
        db.execute(sql('expire_incomingmsg'))
        self.assertEqual(db.execute(sql('incoming_capacity')).fetchone(), (1, 10))
        self.assertEqual(db.execute(sql('get_incomingmsg'), ('new',)).fetchone()[0], 'fresh body')


if __name__ == '__main__':
    unittest.main()
