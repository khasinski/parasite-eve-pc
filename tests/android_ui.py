"""Read the current Android view hierarchy across activity transitions."""
import time
import xml.etree.ElementTree as ET


def read_nodes(adb, path):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        adb('shell', 'rm', '-f', path)
        result = adb('shell', 'uiautomator', 'dump', path)
        if path.encode() in result:
            return list(ET.fromstring(adb('shell', 'cat', path)).iter('node'))
    raise AssertionError('No current Android UI hierarchy')
