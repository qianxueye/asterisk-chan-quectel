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
