#!/usr/bin/env python3
"""
inject_autounlock.py -- minimal DLL injector for BFN (Windows, 64-bit).

Why this exists: AutoUnlock.dll does all of its work from inside the game
process, so "the injection failed" and "the DLL loaded but did nothing" look
identical from the outside. This script injects the DLL itself and then
verifies the result by watching the log file the DLL writes.

Usage:
    python inject_autounlock.py       (or double-click inject_main.bat)

It will:
  1. find PVZBattleforNeighborville.exe
  2. CreateRemoteThread(LoadLibraryW(AutoUnlock.dll))
  3. wait for the thread, print the HMODULE / error code
  4. check whether unlock_log.txt got refreshed within 3 seconds, and echo the
     first lines it contains

English output only. Run it while the game is already running.
"""

import ctypes
import os
import sys
import time
from ctypes import wintypes

HERE = os.path.dirname(os.path.abspath(__file__))
DLL = os.path.join(HERE, "AutoUnlock.dll")
LOG = os.path.join(HERE, "unlock_log.txt")
EXE_NAME = "PVZBattleforNeighborville.exe"

k32 = ctypes.WinDLL("kernel32", use_last_error=True)

PROCESS_ALL_ACCESS = 0x1F0FFF
# what we actually need: create a thread, and read/write/alloc in the target
MINIMAL_RIGHTS = 0x0002 | 0x0008 | 0x0010 | 0x0020 | 0x0400  # THREAD|VM_OP|VM_READ|VM_WRITE|QUERY_INFO
MEM_COMMIT_RESERVE = 0x3000
PAGE_READWRITE = 0x04
INFINITE = 0xFFFFFFFF
TH32CS_SNAPPROCESS = 0x00000002
MAX_PATH = 260


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", wintypes.DWORD),
        ("cntUsage", wintypes.DWORD),
        ("th32ProcessID", wintypes.DWORD),
        ("th32DefaultHeapID", ctypes.POINTER(ctypes.c_ulong)),
        ("th32ModuleID", wintypes.DWORD),
        ("cntThreads", wintypes.DWORD),
        ("th32ParentProcessID", wintypes.DWORD),
        ("pcPriClassBase", ctypes.c_long),
        ("dwFlags", wintypes.DWORD),
        ("szExeFile", ctypes.c_char * MAX_PATH),
    ]


def setup_prototypes():
    """Declare every kernel32 signature we call.

    Not cosmetic. With no restype ctypes assumes c_int and truncates a 64-bit
    return to 32 bits; with no argtypes it truncates every pointer argument the
    same way. A truncated LPVOID is then handed to WriteProcessMemory and
    CreateRemoteThread, and a truncated HANDLE to CloseHandle.
    """
    LPVOID = ctypes.c_void_p
    k32.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    k32.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    k32.Process32First.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
    k32.Process32Next.argtypes = [wintypes.HANDLE, ctypes.POINTER(PROCESSENTRY32)]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]

    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]

    k32.VirtualAllocEx.restype = LPVOID
    k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, LPVOID, ctypes.c_size_t,
                                   wintypes.DWORD, wintypes.DWORD]

    k32.WriteProcessMemory.restype = wintypes.BOOL
    k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, LPVOID, LPVOID, ctypes.c_size_t,
                                       ctypes.POINTER(ctypes.c_size_t)]

    k32.GetModuleHandleW.restype = LPVOID
    k32.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    k32.GetProcAddress.restype = LPVOID
    k32.GetProcAddress.argtypes = [LPVOID, ctypes.c_char_p]

    k32.CreateRemoteThread.restype = wintypes.HANDLE
    k32.CreateRemoteThread.argtypes = [wintypes.HANDLE, LPVOID, ctypes.c_size_t, LPVOID, LPVOID,
                                       wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]

    k32.WaitForSingleObject.restype = wintypes.DWORD
    k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]

    k32.GetExitCodeThread.restype = wintypes.BOOL
    k32.GetExitCodeThread.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]


setup_prototypes()


def find_pid(name):
    snap = k32.CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)
    if not snap or snap == wintypes.HANDLE(-1).value:
        return None
    try:
        entry = PROCESSENTRY32()
        entry.dwSize = ctypes.sizeof(PROCESSENTRY32)
        ok = k32.Process32First(snap, ctypes.byref(entry))
        while ok:
            if entry.szExeFile.decode("mbcs", "replace").lower() == name.lower():
                return entry.th32ProcessID
            ok = k32.Process32Next(snap, ctypes.byref(entry))
    finally:
        k32.CloseHandle(snap)
    return None


def log_mtime():
    try:
        return os.path.getmtime(LOG)
    except OSError:
        return None


def log_head(max_lines=5):
    """The first lines the DLL wrote, i.e. proof it actually ran in this process."""
    out = []
    try:
        with open(LOG, "r", encoding="utf-8", errors="replace") as f:
            for line in f:
                out.append(line.rstrip())
                if len(out) >= max_lines:
                    break
    except OSError:
        pass
    return out


def main():
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        print("[X] this python is not 64-bit; the game is 64-bit.")
        print("    install 64-bit Python, or run the injector from a 64-bit build.")
        return 1

    if not os.path.isfile(DLL):
        print("[X] DLL not found:", DLL)
        print("    run build.bat first.")
        return 1

    print("[*] target DLL :", os.path.basename(DLL))
    print("    (built %s)" % time.strftime("%m-%d %H:%M:%S", time.localtime(os.path.getmtime(DLL))))
    print("[*] target log :", LOG)

    pid = find_pid(EXE_NAME)
    if not pid:
        print("[X] %s is not running. Start the game first." % EXE_NAME)
        return 1
    print("[*] game pid   :", pid)

    before = log_mtime()
    print("[*] log mtime before:", time.strftime("%H:%M:%S", time.localtime(before)) if before else "(none)")

    h = k32.OpenProcess(PROCESS_ALL_ACCESS, False, pid)
    if not h:
        first = ctypes.get_last_error()
        print("[!] OpenProcess(ALL_ACCESS) failed, err=%d - trying minimal rights" % first)
        h = k32.OpenProcess(MINIMAL_RIGHTS, False, pid)
        if not h:
            print("[X] OpenProcess(minimal) failed, err=%d" % ctypes.get_last_error())
            print("    -> run this script from an Administrator command prompt.")
            return 1
        print("[*] minimal rights accepted")
    else:
        print("[*] opened with PROCESS_ALL_ACCESS")

    try:
        path_w = ctypes.c_wchar_p(DLL)
        nbytes = (len(DLL) + 1) * ctypes.sizeof(ctypes.c_wchar)
        remote = k32.VirtualAllocEx(h, None, nbytes, MEM_COMMIT_RESERVE, PAGE_READWRITE)
        if not remote:
            print("[X] VirtualAllocEx failed, err=%d" % ctypes.get_last_error())
            return 1
        print("[*] remote buf  : 0x%X" % remote)

        written = ctypes.c_size_t(0)
        ok = k32.WriteProcessMemory(h, remote, path_w, nbytes, ctypes.byref(written))
        if not ok:
            print("[X] WriteProcessMemory failed, err=%d" % ctypes.get_last_error())
            return 1

        hk = k32.GetModuleHandleW("kernel32.dll")
        addr = k32.GetProcAddress(hk, b"LoadLibraryW")
        print("[*] LoadLibraryW: 0x%X" % (addr or 0))

        th = k32.CreateRemoteThread(h, None, 0, addr, remote, 0, None)
        if not th:
            print("[X] CreateRemoteThread failed, err=%d" % ctypes.get_last_error())
            return 1

        r = k32.WaitForSingleObject(th, 10000)
        code = wintypes.DWORD(0)
        k32.GetExitCodeThread(th, ctypes.byref(code))
        print("[*] thread wait : 0x%X" % r)
        print("[*] HMODULE     : 0x%X  (0 = LoadLibraryW failed inside the game)" % code.value)
    finally:
        k32.CloseHandle(h)

    print("[*] waiting up to 3s for the log to refresh...")
    for _ in range(15):
        time.sleep(0.2)
        now = log_mtime()
        if now != before:
            print("[OK] log refreshed at", time.strftime("%H:%M:%S", time.localtime(now)))
            for line in log_head():
                print("     " + line)
            print("     injected. This window closes now; the DLL opened its own console,")
            print("     do the rest there.")
            return 0

    print("[!] log did NOT refresh.")
    if log_mtime() is None:
        print("    %s does not exist at all." % LOG)
        print("    - the DLL could not open its log file. Its fopen() uses the ANSI code")
        print("      page, so a folder name outside that code page fails silently.")
        print("      FIX: move the DLL and items.txt to an ASCII-only path.")
    print("    - if HMODULE was non-zero: the DLL was ALREADY loaded in this game")
    print("      process (Windows only runs DllMain once per path).")
    print("      FIX: fully close the game, start it again, run this script.")
    print("    - if HMODULE was 0: LoadLibraryW failed inside the game.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
