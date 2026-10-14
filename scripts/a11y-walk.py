
#!/usr/bin/env python3
"""Walk the AT-SPI accessibility tree and report what the app exposes.

There is no accerciser/pyatspi on this machine, but the AT-SPI bus is running
and speakable over D-Bus, so the tree can be read directly with Gio.

  usage: a11y-walk.py [substring-of-app-name] [max-depth]
"""
import sys

from gi.repository import Gio, GLib

ATSPI = "org.a11y.atspi.Accessible"
PROPS = "org.freedesktop.DBus.Properties"
ACTION = "org.a11y.atspi.Action"
TEXT = "org.a11y.atspi.Text"


def prop(bus, name, path, iface, key):
    try:
        res = bus.call_sync(
            name, path, PROPS, "Get",
            GLib.Variant("(ss)", (iface, key)), GLib.VariantType("(v)"),
            Gio.DBusCallFlags.NONE, 3000, None)
        return res.unpack()[0]
    except GLib.Error:
        return None


def children(bus, name, path):
    try:
        res = bus.call_sync(
            name, path, ATSPI, "GetChildren", None,
            GLib.VariantType("(a(so))"), Gio.DBusCallFlags.NONE, 3000, None)
        return [(b, p) for b, p in res.unpack()[0]]
    except GLib.Error:
        return []


def role(bus, name, path):
    for getter, vtype in (("GetRoleName", "(s)"), ("GetRole", "(u)")):
        try:
            res = bus.call_sync(name, path, ATSPI, getter, None,
                                GLib.VariantType(vtype), Gio.DBusCallFlags.NONE, 3000, None)
            value = res.unpack()[0]
            return value if isinstance(value, str) else f"role#{value}"
        except GLib.Error:
            continue
    return "?"


def action_names(bus, name, path):
    try:
        n = bus.call_sync(name, path, ACTION, "GetNActions", None,
                          GLib.VariantType("(i)"), Gio.DBusCallFlags.NONE, 3000, None).unpack()[0]
    except GLib.Error:
        return []
    names = []
    for i in range(n):
        try:
            names.append(bus.call_sync(
                name, path, ACTION, "GetName", GLib.Variant("(i)", (i,)),
                GLib.VariantType("(s)"), Gio.DBusCallFlags.NONE, 3000, None).unpack()[0])
        except GLib.Error:
            pass
    return names


def main():
    want = sys.argv[1] if len(sys.argv) > 1 else "Kin"
    max_depth = int(sys.argv[2]) if len(sys.argv) > 2 else 12

    session = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    addr = session.call_sync(
        "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress", None,
        GLib.VariantType("(s)"), Gio.DBusCallFlags.NONE, 3000, None).unpack()[0]
    bus = Gio.DBusConnection.new_for_address_sync(
        addr, Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT
        | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)

    found = []

    def walk(name, path, depth, prefix):
        if depth > max_depth:
            return
        label = prop(bus, name, path, ATSPI, "Name")
        desc = prop(bus, name, path, ATSPI, "Description")
        r = role(bus, name, path)
        kids = children(bus, name, path)
        acts = action_names(bus, name, path)
        bits = [f"role={r}"]
        if label:
            bits.append(f"name={label!r}")
        if desc:
            bits.append(f"desc={desc!r}")
        if acts:
            bits.append(f"actions={acts}")
        if kids:
            bits.append(f"children={len(kids)}")
        print(f"{prefix}{'  ' * 0}{' '.join(bits)}")
        for b, p in kids:
            walk(b, p, depth + 1, prefix + "    ")

    root = ["org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root"]
    for b, p in children(bus, *root):
        app_name = prop(bus, b, p, ATSPI, "Name") or ""
        if want.lower() in app_name.lower():
            found.append((b, p, app_name))

    if not found:
        print(f"no accessible application matching {want!r}; available:")
        for b, p in children(bus, *root):
            print("   ", prop(bus, b, p, ATSPI, "Name"), b)
        return 1

    for b, p, app_name in found:
        print(f"=== application {app_name!r} on {b} ===")
        walk(b, p, 0, "  ")
    return 0


if __name__ == "__main__":
    sys.exit(main())
