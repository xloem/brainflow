import subprocess
import threading
import socket
import enum
import logging
import sys
import random
import time
import struct

from brainflow_emulator.emulate_common import TestFailureError, log_multilines

def run(*cmd_list, shell=False, input=None):
    logging.info('Running %s' % ' '.join([str(x) for x in cmd_list]))
    process = subprocess.Popen(cmd_list, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=shell)
    stdout, stderr = process.communicate(input=input)

    log_multilines(logging.info, stdout)
    log_multilines(logging.info, stderr)

    return stdout, stderr, process.returncode

def test_socket(cmd_list):

    run('ulimit -c unlimited', shell=True)
    stdout, stderr, returncode = run(*cmd_list)

    if returncode != 0:
        # libsegfault would be better, it doesn't require user installed gdb [should be normal to include something like that]
        gdb_output = run('gdb', cmd_list[0], 'core', input='backtrace\nquit\n')
        raise TestFailureError('Test failed with exit code %s' % str(process.returncode), process.returncode)

    return stdout, stderr


def run_socket_client():
    thread = FasciaEmulator()
    thread.start()
    return thread


class FasciaEmulator(threading.Thread):

    def __init__(self):
        threading.Thread.__init__(self)
        # hardcode 
        self.server_ip = '127.0.0.1'
        self.server_port = 7789
        self.server_socket = socket.socket(family=socket.AF_INET, type=socket.SOCK_DGRAM)
        self.server_socket.settimeout(0.1)
        self.package_num = 0
        self.package_size = 68
        self.package_in_transaction = 20

    def run(self):
        for i in range(100):
            package = list()
            for _ in range(self.package_in_transaction):
                package.extend(bytearray(struct.pack("i", self.package_num)))
                self.package_num = self.package_num + 1
                for i in range(self.package_size - 4):
                    package.append(random.randint(0, 255))
            try:
                self.server_socket.sendto(bytes(package), (self.server_ip, self.server_port))
            except socket.timeout:
                logging.info('timeout for send')

            time.sleep(0.1)


def main(cmd_list):
    if not cmd_list:
        raise Exception('No command to execute')
    server_thread = run_socket_client()
    test_socket(cmd_list)
    server_thread.join()


if __name__ == '__main__':
    logging.basicConfig(format='%(asctime)s %(levelname)-8s %(message)s', level=logging.INFO)
    main(sys.argv[1:])
