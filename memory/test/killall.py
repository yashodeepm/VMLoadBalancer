#!/usr/bin/env python3

from __future__ import print_function
import os, sys
sys.path.append(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from vm import VMManager
from testLibrary import TestLib

VM_PREFIX="aos"

if __name__ == '__main__':
    manager = VMManager()
    vms = manager.getRunningVMNames(VM_PREFIX)
    ips = TestLib.getIps(vms)
    ipsAndVals = { ip : [] for ip in ips }
    TestLib.startTestCase("killall run",ipsAndVals)
