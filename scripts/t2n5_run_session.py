#!/usr/bin/env python3
"""
Hardware execution session for D7-T2N-5: /bin/hello FORMAL HARDWARE SEAL.
Executes fresh-boot hardware validation of native generic Mach-O execution.

Exact sequence:
  pwd        -> / -> xzs#
  /bin/hello -> hello from XNU-XZS userland -> exit(0) -> teardown -> wait4 reap -> EL0 -> xzs#
  pwd        -> / -> xzs#
"""

import importlib.util
import os
import subprocess
import sys
import time
from pathlib import Path

_TOOL = Path(__file__).resolve().parents[1] / "tools" / "xzs-console" / "xzs-console.py"
_spec = importlib.util.spec_from_file_location("xzs_console", _TOOL)
xzs_console = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(xzs_console)


def collect(dev, seconds=1.0):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=200)
        if chunk:
            buf += chunk
        else:
            time.sleep(0.05)
    return buf


def send_cmd(dev, cmd_str, wait_sec=5.0, wait_for_prompt=True):
    print(f"\n>>> SEND: {cmd_str.strip()}", flush=True)
    payload = cmd_str.encode("utf-8") if isinstance(cmd_str, str) else cmd_str
    if not payload.endswith(b"\n"):
        payload += b"\n"
    written, kind = xzs_console.bulk_write(dev, payload, timeout_ms=2000)
    if kind is not None or written != len(payload):
        print(f"!!! WRITE ERROR: written={written}, kind={kind}", flush=True)
        return ""

    end = time.time() + wait_sec
    buf = b""
    quiet = 0
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=200)
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            quiet = 0
            if wait_for_prompt and b"xzs# " in buf[len(payload):]:
                # Found shell prompt after command echo, command is complete
                break
        else:
            quiet += 1
            if not wait_for_prompt and buf and quiet >= 10:
                break
            time.sleep(0.05)
    resp_text = buf.decode("utf-8", errors="replace")
    return resp_text


def main():
    run_num = "1"
    for arg in sys.argv:
        if arg.startswith("--run="):
            run_num = arg.split("=")[1]
        elif arg.startswith("--run_"):
            run_num = arg.split("_")[1]
        elif arg in ("--run1", "-1"):
            run_num = "1"
        elif arg in ("--run2", "-2"):
            run_num = "2"

    rev_res = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], capture_output=True, text=True)
    short_commit = rev_res.stdout.strip() if rev_res.returncode == 0 else "unknown"

    dir1 = Path(f"artifacts/hw/t2n5-{short_commit}/run_{run_num}")
    dir2 = Path(f"artifacts/hw/t2n5-{short_commit}-run_{run_num}")
    dir1.mkdir(parents=True, exist_ok=True)
    dir2.mkdir(parents=True, exist_ok=True)
    log_files = [dir1 / "host.txt", dir2 / "host.txt"]

    boot_img = Path("artifacts/builds/xzs-xnu-boot.img")
    if not boot_img.exists():
        print(f"ERROR: {boot_img} does not exist!", file=sys.stderr)
        sys.exit(1)

    skip_boot = "--no-boot" in sys.argv
    if not skip_boot:
        print(f"=== STEP 0: BOOT CANDIDATE VIA FASTBOOT (RUN {run_num}) ===", flush=True)
        print("Waiting for fastboot device BH905SX976 (up to 120s)...", flush=True)
        fb_found = False
        for _ in range(120):
            fb_check = subprocess.run(["fastboot", "devices"], capture_output=True, text=True)
            if "BH905SX976" in fb_check.stdout:
                fb_found = True
                break
            time.sleep(1)
        if not fb_found:
            print("ERROR: fastboot device BH905SX976 not found!", file=sys.stderr)
            sys.exit(1)
        boot_res = subprocess.run(["fastboot", "-s", "BH905SX976", "boot", str(boot_img)], capture_output=True, text=True)
        print(boot_res.stdout, flush=True)
        print(boot_res.stderr, flush=True)
        if boot_res.returncode != 0:
            print("ERROR: fastboot boot failed!", file=sys.stderr)
            sys.exit(1)
    else:
        print(f"=== STEP 0: SKIPPING FASTBOOT BOOT (RUN {run_num}) ===", flush=True)

    print("=== WAITING FOR USB CONSOLE ENUMERATION ===", flush=True)
    dev = xzs_console.open_stable_device(timeout_sec=120)
    if dev is None:
        print("ERROR: could not open stable XNU USB device", flush=True)
        sys.exit(1)

    print("=== TRANSPORT HANDSHAKE (Z1-Z4) ===", flush=True)
    if not xzs_console.transport_handshake(dev):
        print("ERROR: transport handshake failed", flush=True)
        sys.exit(1)

    time.sleep(0.5)
    initial_drain = collect(dev, seconds=1.5)
    if initial_drain:
        print(initial_drain.decode("utf-8", errors="replace"), end="", flush=True)

    full_log = []
    if initial_drain:
        full_log.append(initial_drain.decode("utf-8", errors="replace"))

    def run_step(name, cmd, wait_s=2.0):
        print(f"\n--- {name} ---", flush=True)
        out = send_cmd(dev, cmd, wait_sec=wait_s, wait_for_prompt=True)
        full_log.append(f"\n# {name}\n> {cmd}\n{out}")
        return out

    # Step 1: Shell health baseline
    pwd_out = run_step("STEP 1: SHELL HEALTH PWD", "pwd\n", wait_s=1.0)

    # Step 2: Execute /bin/hello
    print("\n--- STEP: EXECUTE /bin/hello ---", flush=True)
    hello_out = send_cmd(dev, "/bin/hello\n", wait_sec=12.0, wait_for_prompt=True)
    full_log.append(f"\n# STEP: EXECUTE /bin/hello\n> /bin/hello\n{hello_out}")

    # Step 3: Post-exec health check
    print("\n--- STEP: POST-EXEC HEALTH CHECK ---", flush=True)
    post_out = ""
    try:
        post_out = send_cmd(dev, "pwd\n", wait_sec=3.0, wait_for_prompt=True)
        full_log.append(f"\n# STEP: POST-EXEC PWD\n> pwd\n{post_out}")
    except Exception as e:
        print(f"Post-exec communication check ended: {e}", flush=True)
        full_log.append(f"\n# STEP: POST-EXEC PWD\nException: {e}\n")

    print("\n=== WRITING LOG FILE ===", flush=True)
    log_text = "".join(full_log)
    for lf in log_files:
        lf.write_text(log_text)
        print(f"Logged to {lf}")

    # Session evaluation
    print(f"\n=== EVALUATING D7-T2N-5 RUN {run_num} ===", flush=True)
    baseline_pass = "/" in pwd_out and "xzs# " in pwd_out
    hello_match = "hello from XNU-XZS userland" in hello_out
    exit_status = "0" if ("SYS_EXIT_ENTER status=0" in hello_out or "code=1 r0=0x0" in hello_out) else "unknown"
    old_retired = "yes" if "THREAD_TERMINATE_SELF" in hello_out else "no"
    child_reapable = "yes" if "CHILD_REAPABLE" in hello_out else "no"
    parent_reap = "yes" if "WAIT4_REAP" in hello_out else "no"
    parent_el0 = "yes" if "UNIX_SC_RET pid=1 code=7 err=0" in hello_out else "no"
    prompt_ret = "yes" if "xzs# " in hello_out else "no"
    post_pwd = "/" if ("/" in post_out and "xzs# " in post_out) else "fail"
    has_panic = "panic" in log_text.lower() or "debugger called" in log_text.lower()

    print(f"BASELINE_PWD={'/' if baseline_pass else 'FAIL'}")
    print(f"HELLO_OUTPUT_MATCH={'yes' if hello_match else 'no'}")
    print(f"HELLO_EXIT_STATUS={exit_status}")
    print(f"OLD_EXEC_THREAD_RETIRED={old_retired}")
    print(f"CHILD_REAPABLE={child_reapable}")
    print(f"PARENT_WAIT4_REAP={parent_reap}")
    print(f"PARENT_EL0_RESUME={parent_el0}")
    print(f"PROMPT_RETURNED={prompt_ret}")
    print(f"POST_EXEC_PWD={post_pwd}")
    print(f"PANIC={'yes' if has_panic else 'no'}")

    run_pass = (baseline_pass and hello_match and exit_status == "0" and
                parent_reap == "yes" and prompt_ret == "yes" and
                post_pwd == "/" and not has_panic)
    print(f"RUN_{run_num}_VERDICT={'PASS' if run_pass else 'FAIL'}")
    print(f"=== D7-T2N-5 (RUN {run_num}) HARDWARE SESSION COMPLETE ===", flush=True)


if __name__ == "__main__":
    main()
