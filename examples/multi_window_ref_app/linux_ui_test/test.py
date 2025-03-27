"""Automated UI testing using pywinauto."""

# Requires pywinauto to be manually installed from the master branch:
# https://github.com/pywinauto/pywinauto/tree/master
# https://pywinauto.readthedocs.io/en/latest/#manual-installation

import time

from pywinauto import Application, keyboard


app = Application(backend="atspi")
window = app.start("gnome-calculator").window()

# Print control identifiers for debugging
# window.dump_tree(depth=None, max_width=None)

# Performs a calculation by clicking buttons
two_button = window["2"].click()
time.sleep(0.1)
window["1"].click()
time.sleep(0.1)
window["\N{MULTIPLICATION SIGN}"].click()
time.sleep(0.1)
window["2"].click()
time.sleep(0.1)
window["="].click()
time.sleep(0.1)
assert window["42"].exists()

# Performs the same calculation using the keyboard
scroll_pane = window["Panel2"].set_keyboard_focus()
keyboard.send_keys("{ESC}21*2{ENTER}")
assert window["42"].exists()

app.kill()
