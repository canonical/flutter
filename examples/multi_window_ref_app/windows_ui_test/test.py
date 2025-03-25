"""Automated UI testing of the Flutter Multi-Window Reference Application using pywinauto."""

import time
import shlex
import subprocess
import pytest
from pywinauto import Desktop, timings

DIR = ".."
DART_FILE = "lib/main.dart"
ENGINE_TARGET = "host_debug_unopt"

# Element titles
MAIN_WINDOW_TITLE = "Multi-Window Reference Application"
REGULAR = "Regular"
CLOSE = "Close"
CREATE_REGULAR_WINDOW = "Create Regular Window"


def wait_for_window(title, timeout=60):
    """Wait for a window with specified title to become available."""
    desktop = Desktop(backend="uia")
    window = desktop.window(title=title)
    try:
        window.wait("ready", timeout=timeout, retry_interval=1)
    except timings.TimeoutError:
        return None
    return window


def wait_for_first_frame():
    """
    Wait for some time to ensure the first frame is presented.
    This can be removed once windows are created as hidden.
    """
    time.sleep(15)


@pytest.fixture(scope="function")
def main_window():
    """Fixture to start and stop the Flutter application."""
    proc = None
    main_win = None
    try:
        # Invoke flutter run
        proc = subprocess.Popen(
            shlex.split(
                f"flutter run --local-engine {ENGINE_TARGET} "
                f"--local-engine-host {ENGINE_TARGET} "
                f"--enable-multi-window {DART_FILE}"
            ),
            cwd=DIR,
            shell=True,
            text=True,
            stdin=subprocess.PIPE,
        )

        # Wait for main application window
        main_win = wait_for_window(MAIN_WINDOW_TITLE)
        if not main_win:
            pytest.fail(f"{MAIN_WINDOW_TITLE} did not open in time")

        # Detach from flutter run
        proc.stdin.write("d")
        proc.stdin.flush()

        yield main_win

    finally:
        if main_win and main_win.exists():
            main_win.type_keys("%{F4}")

        if proc and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


def test_main_window_opens(main_window):
    """Test that the main application window opens."""
    assert main_window.exists()


def test_regular_window_opens(main_window):
    """Test that clicking the Regular button opens a regular window."""
    wait_for_first_frame()

    regular_btn = main_window.child_window(title=REGULAR, control_type="Button")
    regular_btn.click_input()

    regular_win = wait_for_window(REGULAR)
    assert regular_win is not None and regular_win.exists()


def test_regular_window_closes(main_window):
    """Test closing a regular window."""
    wait_for_first_frame()

    regular_btn = main_window.child_window(title=REGULAR, control_type="Button")
    regular_btn.click_input()

    regular_win = wait_for_window(REGULAR)
    assert regular_win.exists()

    close_btn = regular_win.child_window(title=CLOSE, control_type="Button")
    close_btn.click_input()

    regular_win.wait_not("exists", timeout=10)
    assert wait_for_window(REGULAR, timeout=1) is None


def test_multiple_regular_windows(main_window):
    """Test creating multiple regular windows."""
    wait_for_first_frame()

    # Create first window
    regular_btn = main_window.child_window(title=REGULAR, control_type="Button")
    regular_btn.click_input()

    regular_win1 = wait_for_window(REGULAR)
    assert regular_win1.exists()

    # Create second window
    regular_btn = regular_win1.child_window(
        title=CREATE_REGULAR_WINDOW, control_type="Button"
    )
    regular_btn.click_input()

    time.sleep(2)

    desktop = Desktop(backend="uia")
    assert len(desktop.windows(title=REGULAR)) == 2


if __name__ == "__main__":
    pytest.main(["-vv", __file__])
