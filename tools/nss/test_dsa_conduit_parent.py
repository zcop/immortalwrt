#!/usr/bin/env python3
"""Exercise the real system_if_get_parent() DSA conduit guard; no netifd build claim."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PATCH = (ROOT / 'package/network/config/netifd/patches' /
         '100-dsa-keep-conduit-lifecycle-with-kernel.patch')

FUNCTION = re.compile(
    r'^struct device \*\s*\n'
    r'system_if_get_parent\(struct device \*dev\)\s*\n'
    r'\{.*?^\}',
    re.M | re.S)

PREAMBLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KERNEL_VERSION(a, b, c) (((a) << 16) + ((b) << 8) + (c))
#define LINUX_VERSION_CODE KERNEL_VERSION(@KERNEL@)

struct device {
	const char *ifname;
};

#define CONDUIT_IFINDEX 10
#define USER_IFINDEX 20

static struct device user_dev = { .ifname = "lan1" };
static struct device parent_dev = { .ifname = "eth1" };

static int sysfs_rc;
static int iflink_value;
static int resolve_value;
static char name_result[16];
static int name_fail;
static int parent_calls;
static char parent_name[16];

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
static int master_ifindex;
static int master_calls;
#endif

static int system_get_dev_sysfs(const char *file, const char *ifname,
				char *buf, size_t buf_sz)
{
	assert(!strcmp(file, "iflink"));
	assert(!strcmp(ifname, user_dev.ifname));

	if (sysfs_rc < 0)
		return sysfs_rc;

	snprintf(buf, buf_sz, "%d", iflink_value);
	return 0;
}

static int system_if_resolve(struct device *dev)
{
	assert(dev == &user_dev);
	return resolve_value;
}

static char *test_if_indextoname(unsigned int ifindex, char *ifname)
{
	assert(ifindex == (unsigned int)iflink_value);
	if (name_fail)
		return NULL;

	strcpy(ifname, name_result);
	return ifname;
}
#define if_indextoname test_if_indextoname

static struct device *device_get(const char *ifname, bool create)
{
	assert(create);
	parent_calls++;
	snprintf(parent_name, sizeof(parent_name), "%s", ifname);
	return &parent_dev;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
static __attribute__((unused)) int
system_if_get_master_ifindex(struct device *dev)
{
	assert(dev == &user_dev);
	master_calls++;
	return master_ifindex;
}
#endif

static void reset(void)
{
	sysfs_rc = 0;
	iflink_value = CONDUIT_IFINDEX;
	resolve_value = USER_IFINDEX;
	strcpy(name_result, "eth1");
	name_fail = 0;
	parent_calls = 0;
	parent_name[0] = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
	master_ifindex = -1;
	master_calls = 0;
#endif
}
'''

TESTS = r'''
int main(int argc, char **argv)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
	if (argc == 2 && !strcmp(argv[1], "dsa-null")) {
		struct device *parent;

		reset();
		master_ifindex = CONDUIT_IFINDEX;
		parent = system_if_get_parent(&user_dev);
		printf("DSA parent %s\n", parent ? "created" : "none");
		return parent ? 1 : 0;
	}

	/* DSA user port: the kernel-owned conduit must not become a parent */
	reset();
	master_ifindex = CONDUIT_IFINDEX;
	assert(system_if_get_parent(&user_dev) == NULL);
	assert(parent_calls == 0);
#endif

	/* Ordinary VLAN/other netdev: iflink parent discovery is unchanged */
	reset();
	assert(system_if_get_parent(&user_dev) == &parent_dev);
	assert(parent_calls == 1);
	assert(!strcmp(parent_name, "eth1"));
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
	assert(master_calls == 1);
#endif

	/* iflink equal to the device's own ifindex means no parent */
	reset();
	iflink_value = USER_IFINDEX;
	assert(system_if_get_parent(&user_dev) == NULL);
	assert(parent_calls == 0);

	/* iflink read failure */
	reset();
	sysfs_rc = -1;
	assert(system_if_get_parent(&user_dev) == NULL);
	assert(parent_calls == 0);

	/* Missing iflink */
	reset();
	iflink_value = 0;
	resolve_value = 0;
	assert(system_if_get_parent(&user_dev) == NULL);
	assert(parent_calls == 0);

	/* parent name lookup failure */
	reset();
	name_fail = 1;
	assert(system_if_get_parent(&user_dev) == NULL);
	assert(parent_calls == 0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6,1,0)
	/* DSA helper error: fall back to ordinary parent discovery */
	reset();
	master_ifindex = -1;
	assert(system_if_get_parent(&user_dev) == &parent_dev);
	assert(parent_calls == 1);

	/* DSA helper detection wins over unreadable iflink metadata */
	reset();
	master_ifindex = CONDUIT_IFINDEX;
	sysfs_rc = -1;
	assert(system_if_get_parent(&user_dev) == NULL);
	assert(parent_calls == 0);
#else
	/* pre-6.1: no DSA helper, iflink parent discovery remains intact */
	reset();
	assert(system_if_get_parent(&user_dev) == &parent_dev);
	assert(parent_calls == 1);
#endif

	puts("PASS: DSA conduit guard, ordinary parent discovery, self-iflink, "
	     "iflink/name failures, helper fallback and pre-6.1 path");
	return 0;
}
'''


def extract(text, label):
    match = FUNCTION.search(text)
    assert match, f'cannot extract system_if_get_parent from {label}'
    return match.group(0)


def harness(kernel, function):
    return PREAMBLE.replace('@KERNEL@', kernel) + '\n' + function + '\n' + TESTS


def build(directory, name, text):
    cfile = directory / f'{name}.c'
    cfile.write_text(text)
    executable = directory / name
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    str(cfile), '-o', str(executable)], check=True)
    return executable


def run(executable, *arguments):
    environment = dict(os.environ)
    environment['ASAN_OPTIONS'] = environment.get('ASAN_OPTIONS', '') + ':detect_leaks=0'
    return subprocess.run([str(executable), *arguments], capture_output=True,
                          text=True, env=environment)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True,
                        help='pristine netifd system-linux.c')
    args = parser.parse_args()

    source = args.source.read_text()
    original_function = extract(source, str(args.source))

    with tempfile.TemporaryDirectory(prefix='netifd-dsa-parent-') as temporary:
        directory = Path(temporary)
        copy = directory / 'system-linux.c'
        copy.write_text(source)

        subprocess.run(['patch', '-p1', '--batch', '--forward', '--fuzz=0',
                        '-i', str(PATCH)], cwd=directory, check=True,
                       capture_output=True, text=True)

        patched = copy.read_text()
        assert patched != source, 'patch left the source unchanged'
        patched_function = extract(patched, 'patched source')

        modern = build(directory, 'patched-6.18',
                       harness('6,18,0', patched_function))
        legacy = build(directory, 'patched-5.15',
                       harness('5,15,0', patched_function))
        original = build(directory, 'original-6.18',
                         harness('6,18,0', original_function))

        result = run(modern)
        assert result.returncode == 0, result.stderr
        assert 'PASS' in result.stdout, result.stdout

        result = run(modern, 'dsa-null')
        assert result.returncode == 0, (result.stdout, result.stderr)
        assert 'DSA parent none' in result.stdout, result.stdout

        result = run(original, 'dsa-null')
        assert result.returncode != 0, \
            'pristine source unexpectedly suppressed the DSA parent'
        assert 'DSA parent created' in result.stdout, result.stdout

        result = run(legacy)
        assert result.returncode == 0, result.stderr
        assert 'PASS' in result.stdout, result.stdout

    print('PASS: DSA conduits stay out of netifd parent discovery; '
          'the pristine source still creates the conduit parent')


if __name__ == '__main__':
    main()
