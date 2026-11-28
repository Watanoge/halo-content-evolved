"""The host's tag table: a client of another disc's maps finds the host's tags by group and path."""
import re
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from harness import CHECK_FAILED, build, mutated, read, run
from test_aim_assist import message_ids

CONTROLS = {'no-salt-check': (' || tag_table.host[absolute].datum != tag_index', '', 'other-disc'),
            'parts-out-of-order': ('message.first != tag_table.received ||', '', 'order'),
            'identity-translated': ('if (same_until == local_count && same_until == tag_table.host_count)',
                                    'if (0)', 'identity'),
            'any-sender': ('if (machine_index != NONE ||', 'if (', 'authority'),
            'last-duplicate-wins': ('\t\t\thost_index++;\n\t\t\tlocal_index++;\n\t\t}',
                                    '\t\t\thost_index++;\n\t\t}', 'duplicates'),
            'reset-kept': ('\ttag_table_free();\n}', '}', 'other-disc'),
            'client-not-recorded': ('\t\tnetwork_replay_record(_network_replay_reliable, &message, size);', '', 'replay'),
            'host-not-recorded': ('\t\tnetwork_tag_table_send(DISTRIBUTED_RECORDER_MACHINE);', '', 'replay')}


def generated(fault=None):
    code = re.sub(r'^#include .*$', '', read('port/linux/game/network_tag_table.c'), flags=re.M)
    # (the 32-bit ABI's sizes: a check of the layout, not of what it does)
    code = re.sub(r'^typedef char \w+_assert\[.*$', '', code, flags=re.M)
    code = code.replace('extern void *cache_files_tag_instances(long *count);',
                        'void *cache_files_tag_instances(long *count);')
    if fault:
        before, after, _ = CONTROLS[fault]
        code = mutated(code, before, after)
    return (('under_test.inc', code), ('message_ids.inc', message_ids()))


@pytest.mark.parametrize('case', ['identity', 'other-disc', 'duplicates', 'order', 'replay', 'authority'])
def test_case(case):
    status, output = run(build('network_tag_table', generated()), case)
    assert status == 0, output


@pytest.mark.parametrize('fault', sorted(CONTROLS))
def test_negative_control(fault):
    status, output = run(build('network_tag_table', generated(fault)), CONTROLS[fault][2])
    assert status == CHECK_FAILED and 'check failed:' in output, output


def test_message_id():
    assert re.search(r'_distributed_message_tag_table = 169,', read('port/linux/game/network_distributed.h'))
