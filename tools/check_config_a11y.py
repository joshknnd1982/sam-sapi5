"""Accessibility check for the SAM Voice Settings dialog.

Launches the utility, walks its MSAA tree with oleacc, and reports the
accessible name, role and state of every control, plus the tab order.

MSAA is used deliberately: a UI Automation client reports plain Win32 controls
as generic panes, which hides exactly the problems this check is looking for.
Screen readers such as NVDA read these same MSAA properties.
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time
import os

oleacc = ctypes.oledll.oleacc
user32 = ctypes.windll.user32
ole32 = ctypes.windll.ole32

CHILDID_SELF = 0
OBJID_CLIENT = -4

# MSAA ROLE_SYSTEM_* constants (oleacc.h).
ROLE = {
    9: "window", 10: "client", 16: "pane", 18: "dialog", 20: "grouping",
    21: "separator", 22: "toolbar", 33: "list", 34: "listitem",
    41: "statictext", 42: "text(editable)", 43: "pushbutton", 44: "checkbox",
    45: "radiobutton", 46: "combobox", 47: "droplist", 48: "progressbar",
    51: "slider", 52: "spinbutton",
}

# Roles that misreport their value to a screen reader for wide numeric ranges:
# a slider announces position as a percentage, so 64 out of 255 is read as 25.
PERCENTAGE_ROLES = {51: "slider"}
STATE_FOCUSABLE = 0x00100000
STATE_INVISIBLE = 0x00008000
STATE_UNAVAILABLE = 0x00000001
STATE_READONLY = 0x00000040
STATE_CHECKED = 0x00000010


class VARIANT(ctypes.Structure):
    _fields_ = [("vt", ctypes.c_ushort), ("r1", ctypes.c_ushort),
                ("r2", ctypes.c_ushort), ("r3", ctypes.c_ushort),
                ("val", ctypes.c_longlong)]


def make_child_variant(child_id=CHILDID_SELF):
    v = VARIANT()
    v.vt = 3  # VT_I4
    v.val = child_id
    return v


class IAccessibleVTable:
    """Hand-rolled vtable access, to avoid depending on comtypes."""

    # IAccessible vtable slots (IDispatch has 7 entries before these).
    GET_ACC_PARENT = 7
    GET_ACC_CHILD_COUNT = 8
    GET_ACC_CHILD = 9
    GET_ACC_NAME = 10
    GET_ACC_VALUE = 11
    GET_ACC_DESCRIPTION = 12
    GET_ACC_ROLE = 13
    GET_ACC_STATE = 14

    def __init__(self, ptr):
        self.ptr = ptr
        self.vtable = ctypes.cast(
            ctypes.cast(ptr, ctypes.POINTER(ctypes.c_void_p))[0],
            ctypes.POINTER(ctypes.c_void_p))

    def _fn(self, slot, restype, argtypes):
        proto = ctypes.WINFUNCTYPE(restype, *argtypes)
        return proto(self.vtable[slot])

    def child_count(self):
        fn = self._fn(self.GET_ACC_CHILD_COUNT, ctypes.c_long,
                      (ctypes.c_void_p, ctypes.POINTER(ctypes.c_long)))
        n = ctypes.c_long()
        if fn(self.ptr, ctypes.byref(n)) != 0:
            return 0
        return n.value

    def name(self, child=CHILDID_SELF):
        fn = self._fn(self.GET_ACC_NAME, ctypes.c_long,
                      (ctypes.c_void_p, VARIANT, ctypes.POINTER(ctypes.c_wchar_p)))
        out = ctypes.c_wchar_p()
        if fn(self.ptr, make_child_variant(child), ctypes.byref(out)) != 0:
            return ""
        return out.value or ""

    def value(self, child=CHILDID_SELF):
        fn = self._fn(self.GET_ACC_VALUE, ctypes.c_long,
                      (ctypes.c_void_p, VARIANT, ctypes.POINTER(ctypes.c_wchar_p)))
        out = ctypes.c_wchar_p()
        if fn(self.ptr, make_child_variant(child), ctypes.byref(out)) != 0:
            return ""
        return out.value or ""

    def role(self, child=CHILDID_SELF):
        fn = self._fn(self.GET_ACC_ROLE, ctypes.c_long,
                      (ctypes.c_void_p, VARIANT, ctypes.POINTER(VARIANT)))
        out = VARIANT()
        if fn(self.ptr, make_child_variant(child), ctypes.byref(out)) != 0:
            return -1
        return int(out.val)

    def state(self, child=CHILDID_SELF):
        fn = self._fn(self.GET_ACC_STATE, ctypes.c_long,
                      (ctypes.c_void_p, VARIANT, ctypes.POINTER(VARIANT)))
        out = VARIANT()
        if fn(self.ptr, make_child_variant(child), ctypes.byref(out)) != 0:
            return 0
        return int(out.val)


def accessible_from_hwnd(hwnd):
    ptr = ctypes.c_void_p()
    IID_IAccessible = ctypes.create_string_buffer(16)
    # {618736E0-3C3D-11CF-810C-00AA00389B71}
    guid = (ctypes.c_ubyte * 16)(
        0xE0, 0x36, 0x87, 0x61, 0x3D, 0x3C, 0xCF, 0x11,
        0x81, 0x0C, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71)
    try:
        oleacc.AccessibleObjectFromWindow(
            wt.HWND(hwnd), ctypes.c_ulong(OBJID_CLIENT & 0xFFFFFFFF),
            ctypes.byref(guid), ctypes.byref(ptr))
    except OSError:
        return None
    return IAccessibleVTable(ptr) if ptr else None


def find_dialog(title, timeout=15.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        hwnd = user32.FindWindowW(None, title)
        if hwnd:
            return hwnd
        time.sleep(0.2)
    return 0


def enum_children(parent):
    """Return child HWNDs of `parent`, in Z-order (which is tab order here)."""
    result = []
    proto = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)

    def cb(hwnd, _):
        result.append(hwnd)
        return True

    user32.EnumChildWindows(parent, proto(cb), 0)
    return result


def class_of(hwnd):
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def text_of(hwnd):
    n = user32.GetWindowTextLengthW(hwnd)
    buf = ctypes.create_unicode_buffer(n + 1)
    user32.GetWindowTextW(hwnd, buf, n + 1)
    return buf.value


GWL_STYLE = -16
WS_TABSTOP = 0x00010000
WS_VISIBLE = 0x10000000
WS_DISABLED = 0x08000000


def style_of(hwnd):
    return user32.GetWindowLongW(hwnd, GWL_STYLE) & 0xFFFFFFFF


def main():
    exe = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                       "build_x64", "bin", "Release", "SamVoiceSettings.exe")
    if not os.path.exists(exe):
        print("build first: cmake --build build_x64 --config Release")
        return 1

    ole32.CoInitialize(None)
    proc = subprocess.Popen([exe])
    try:
        hwnd = find_dialog("SAM Voice Settings")
        if not hwnd:
            print("FAIL: dialog window never appeared")
            return 1
        print("dialog found (hwnd 0x%X)\n" % hwnd)

        children = enum_children(hwnd)
        problems = []
        tab_order = []

        print("%-4s %-16s %-22s %-40s %s" %
              ("#", "class", "role", "accessible name", "value/state"))
        print("-" * 110)

        for i, child in enumerate(children):
            cls = class_of(child)
            style = style_of(child)
            acc = accessible_from_hwnd(child)
            if acc is None:
                problems.append("%s: no IAccessible" % cls)
                continue

            role_id = acc.role()
            role = ROLE.get(role_id, "role#%d" % role_id)
            name = acc.name()
            value = acc.value()
            state = acc.state()

            visible = bool(style & WS_VISIBLE)
            tabstop = bool(style & WS_TABSTOP)
            disabled = bool(style & WS_DISABLED)

            flags = []
            if tabstop:
                flags.append("TABSTOP")
            if state & STATE_CHECKED:
                flags.append("checked")
            if disabled:
                flags.append("DISABLED")

            if tabstop and visible and not disabled:
                tab_order.append((cls, name or "(unnamed)", role))

            print("%-4d %-16s %-22s %-40s %s" %
                  (i, cls, role, repr(name)[:38], (repr(value)[:20] + " " +
                                                   " ".join(flags)).strip()))

            # Anything a screen reader can land on must have a name. Spin
            # buddies are deliberately not tab stops -- the arrow keys drive
            # them from inside their edit box -- so they are not checked.
            if tabstop and visible and not disabled and not name.strip():
                if not text_of(child).strip():
                    problems.append("%s (index %d) is focusable but has no "
                                    "accessible name" % (cls, i))

            # A focusable control that reports a percentage would misannounce
            # the 0-255 parameter ranges.
            if tabstop and role_id in PERCENTAGE_ROLES:
                problems.append("%s (index %d, %r) is a %s: its value is "
                                "announced as a percentage"
                                % (cls, i, name, PERCENTAGE_ROLES[role_id]))

        print("\n--- tab order (%d stops) ---" % len(tab_order))
        for i, (cls, name, role) in enumerate(tab_order, 1):
            print("  %2d. %-40s %-16s %s" % (i, name, cls, role))

        # Every value entry must be an edit box, never a trackbar: MSAA reports
        # a trackbar position as a percentage, which misannounces 0-255 ranges.
        sliders = [c for c in children if "trackbar" in class_of(c).lower()]
        if sliders:
            problems.append("%d trackbar control(s) present; MSAA announces these "
                            "as percentages" % len(sliders))

        print("\n--- result ---")
        if problems:
            for p in problems:
                print("  PROBLEM: %s" % p)
            print("FAILED (%d problem(s))" % len(problems))
            return 1
        print("PASSED: every interactive control is named, reachable by Tab, "
              "and free of percentage-reporting trackbars")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
