#!/usr/bin/env python3
"""
Hardware execution session runner for D7-T2N-6: GENERIC ARGS & REPEATED EXEC STABILITY.

Modes:
  --mode=t2n6a [--no-boot]
      Executes T2N-6A argument correctness tests:
        1. pwd (health baseline)
        2. /bin/args one two three (baseline)
        3. pwd (post-baseline)
        4. /bin/args (Case A: no extra args)
        5. /bin/args test (Case B: one arg)
        6. /bin/args a bb ccc dddd (Case C: multiple short args)
        7. pwd (final post-exec check)

  --mode=mixed --run=1 [--no-boot]
      Executes T2N-6B Mixed Run 1 (Fresh Boot):
        1. pwd
        2. /bin/hello       (EXEC_1)
        3. /bin/args a b c  (EXEC_2)
        4. /bin/hello       (EXEC_3)
        5. /bin/args x y    (EXEC_4)
        6. /bin/hello       (EXEC_5)
        7. pwd

  --mode=mixed --run=2 [--no-boot]
      Executes T2N-6B Mixed Run 2 (Second Fresh Boot):
        Identical mixed sequence as Run 1.
"""

import importlib.util
import os
import re
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


def send_cmd(dev, cmd_str, wait_sec=10.0, wait_for_prompt=True):
    time.sleep(0.5)
    pre = collect(dev, seconds=0.2)
    if pre:
        sys.stdout.write(pre.decode("utf-8", errors="replace"))
        sys.stdout.flush()

    print(f"\n>>> SEND: {cmd_str.strip()}", flush=True)
    payload = cmd_str.encode("utf-8") if isinstance(cmd_str, str) else cmd_str
    if not payload.endswith(b"\n"):
        payload += b"\n"
    written, kind = xzs_console.bulk_write(dev, payload, timeout_ms=3000)
    if kind is not None or written != len(payload):
        print(f"!!! WRITE ERROR: written={written}, kind={kind}", flush=True)
        return ""

    end = time.time() + wait_sec
    buf = b""
    while time.time() < end:
        chunk, kind = xzs_console.bulk_read(dev, timeout_ms=200)
        if chunk:
            buf += chunk
            sys.stdout.write(chunk.decode("utf-8", errors="replace"))
            sys.stdout.flush()
            if wait_for_prompt and b"xzs# " in buf:
                time.sleep(0.2)
                trailing = collect(dev, seconds=0.2)
                if trailing:
                    buf += trailing
                    sys.stdout.write(trailing.decode("utf-8", errors="replace"))
                    sys.stdout.flush()
                break
        else:
            time.sleep(0.05)
    return buf.decode("utf-8", errors="replace")


def boot_device(run_desc):
    boot_img = Path("artifacts/builds/xzs-xnu-boot.img")
    if not boot_img.exists():
        print(f"ERROR: {boot_img} does not exist!", file=sys.stderr)
        sys.exit(1)

    print(f"=== BOOT CANDIDATE VIA FASTBOOT ({run_desc}) ===", flush=True)
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


def connect_console(skip_boot=False):
    if not skip_boot:
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
        return dev, initial_drain.decode("utf-8", errors="replace") if initial_drain else ""
    else:
        print("=== CONNECTING TO EXISTING USB CONSOLE ===", flush=True)
        dev = xzs_console.find_xzs_device()
        if dev is None:
            print("ERROR: existing XNU USB device not found!", flush=True)
            sys.exit(1)
        initial_drain = collect(dev, seconds=0.5)
        return dev, initial_drain.decode("utf-8", errors="replace") if initial_drain else ""


def parse_exec_result(output_text, expected_cmd):
    # Extracts PID, exit code, reap, prompt
    # Example: [XZS-SC] pid=9 code=1 r0=0x0
    # [XZS-T4R] CP=WAIT4_REAP PARENT_PID=1 CHILD_PID=9
    pid_match = re.search(r"target_pid=(\d+)", output_text)
    if not pid_match:
        pid_match = re.search(r"pid=(\d+).*SYS_EXIT_ENTER", output_text)
    pid = pid_match.group(1) if pid_match else "unknown"

    exit_status = "unknown"
    if f"pid={pid} code=1 r0=0x0" in output_text or f"PID={pid} SYS_EXIT_ENTER status=0" in output_text:
        exit_status = "0"
    elif "status=0" in output_text:
        exit_status = "0"

    reap_match = (f"WAIT4_REAP PARENT_PID=1 CHILD_PID={pid}" in output_text or
                  "WAIT4_REAP" in output_text or
                  f"PARENT_WAIT4_RETURN child_pid={pid}" in output_text or
                  f"code=7 err=0 r0=0x{int(pid):x}" in output_text if pid.isdigit() else False)
    prompt_match = "xzs# " in output_text

    return {
        "pid": pid,
        "exit_status": exit_status,
        "reaped": "yes" if reap_match else "no",
        "prompt": "yes" if prompt_match else "no"
    }


def run_t2n6a_mode(dev, initial_log, short_commit):
    out_dir = Path(f"artifacts/hw/t2n6-{short_commit}/t2n6a")
    out_dir.mkdir(parents=True, exist_ok=True)
    log_file = out_dir / "host.txt"

    full_log = [initial_log] if initial_log else []

    def execute_step(step_name, cmd, wait_s=6.0):
        print(f"\n--- {step_name} ---", flush=True)
        res = send_cmd(dev, cmd, wait_sec=wait_s, wait_for_prompt=True)
        full_log.append(f"\n# {step_name}\n> {cmd}\n{res}")
        return res

    pwd_pre = execute_step("STEP 1: BASELINE PWD", "pwd\n", wait_s=2.0)
    baseline_out = execute_step("STEP 2: BASELINE /bin/args one two three", "/bin/args one two three\n", wait_s=10.0)
    pwd_post_base = execute_step("STEP 3: POST-BASELINE PWD", "pwd\n", wait_s=2.0)
    case_a_out = execute_step("STEP 4: CASE A /bin/args", "/bin/args\n", wait_s=8.0)
    case_b_out = execute_step("STEP 5: CASE B /bin/args test", "/bin/args test\n", wait_s=8.0)
    case_c_out = execute_step("STEP 6: CASE C /bin/args a bb ccc dddd", "/bin/args a bb ccc dddd\n", wait_s=8.0)
    pwd_final = execute_step("STEP 7: FINAL PWD", "pwd\n", wait_s=2.0)

    log_text = "".join(full_log)
    log_file.write_text(log_text)
    print(f"\nLogged to {log_file}")

    # Evaluate
    print("\n=== EVALUATING T2N-6A ===", flush=True)
    b_res = parse_exec_result(baseline_out, "/bin/args")
    ca_res = parse_exec_result(case_a_out, "/bin/args")
    cb_res = parse_exec_result(case_b_out, "/bin/args")
    cc_res = parse_exec_result(case_c_out, "/bin/args")

    print(f"BASELINE: PID={b_res['pid']} EXIT={b_res['exit_status']} REAP={b_res['reaped']} PROMPT={b_res['prompt']}")
    print(f"CASE_A:   PID={ca_res['pid']} EXIT={ca_res['exit_status']} REAP={ca_res['reaped']} PROMPT={ca_res['prompt']}")
    print(f"CASE_B:   PID={cb_res['pid']} EXIT={cb_res['exit_status']} REAP={cb_res['reaped']} PROMPT={cb_res['prompt']}")
    print(f"CASE_C:   PID={cc_res['pid']} EXIT={cc_res['exit_status']} REAP={cc_res['reaped']} PROMPT={cc_res['prompt']}")

    def verify_args_step(step_out, exp_argc, exp_args, res):
        has_argc = ("argc=" in step_out and str(exp_argc) in step_out)
        all_args = all(a in step_out for a in exp_args)
        has_exit0 = (res["exit_status"] == "0")
        has_reap = (res["reaped"] == "yes")
        has_prompt = (res["prompt"] == "yes")
        return has_argc and all_args and has_exit0 and has_reap and has_prompt

    b_ok = verify_args_step(baseline_out, 4, ["/bin/args", "one", "two", "three"], b_res)
    ca_ok = verify_args_step(case_a_out, 1, ["/bin/args"], ca_res)
    cb_ok = verify_args_step(case_b_out, 2, ["/bin/args", "test"], cb_res)
    cc_ok = verify_args_step(case_c_out, 5, ["/bin/args", "a", "bb", "ccc", "dddd"], cc_res)

    pwd_post_ok = ("/" in pwd_post_base and "xzs# " in pwd_post_base)
    pwd_final_ok = ("/" in pwd_final and "xzs# " in pwd_final)
    has_panic = "panic" in log_text.lower() or "debugger called" in log_text.lower()

    t2n6a_pass = b_ok and ca_ok and cb_ok and cc_ok and pwd_post_ok and pwd_final_ok and not has_panic
    print(f"BASELINE_OK={'yes' if b_ok else 'no'}")
    print(f"CASE_A_OK={'yes' if ca_ok else 'no'}")
    print(f"CASE_B_OK={'yes' if cb_ok else 'no'}")
    print(f"CASE_C_OK={'yes' if cc_ok else 'no'}")
    print(f"PWD_POST_BASELINE={'/' if pwd_post_ok else 'FAIL'}")
    print(f"PWD_FINAL={'/' if pwd_final_ok else 'FAIL'}")
    print(f"PANIC={'yes' if has_panic else 'no'}")
    print(f"T2N-6A VERDICT={'PASS' if t2n6a_pass else 'FAIL'}")


def run_mixed_mode(dev, initial_log, short_commit, run_num):
    out_dir = Path(f"artifacts/hw/t2n6-{short_commit}/mixed_run_{run_num}")
    out_dir.mkdir(parents=True, exist_ok=True)
    log_file = out_dir / "host.txt"

    full_log = [initial_log] if initial_log else []

    def execute_step(step_name, cmd, wait_s=6.0):
        print(f"\n--- {step_name} ---", flush=True)
        res = send_cmd(dev, cmd, wait_sec=wait_s, wait_for_prompt=True)
        full_log.append(f"\n# {step_name}\n> {cmd}\n{res}")
        return res

    pwd_pre = execute_step("STEP 1: INITIAL PWD", "pwd\n", wait_s=2.0)
    e1_out = execute_step("STEP 2: EXEC_1 /bin/hello", "/bin/hello\n", wait_s=10.0)
    e2_out = execute_step("STEP 3: EXEC_2 /bin/args a b c", "/bin/args a b c\n", wait_s=10.0)
    e3_out = execute_step("STEP 4: EXEC_3 /bin/hello", "/bin/hello\n", wait_s=10.0)
    e4_out = execute_step("STEP 5: EXEC_4 /bin/args x y", "/bin/args x y\n", wait_s=10.0)
    e5_out = execute_step("STEP 6: EXEC_5 /bin/hello", "/bin/hello\n", wait_s=10.0)
    pwd_post = execute_step("STEP 7: FINAL PWD", "pwd\n", wait_s=2.0)

    log_text = "".join(full_log)
    log_file.write_text(log_text)
    print(f"\nLogged to {log_file}")

    print(f"\n=== EVALUATING MIXED RUN {run_num} ===", flush=True)
    r1 = parse_exec_result(e1_out, "/bin/hello")
    r2 = parse_exec_result(e2_out, "/bin/args")
    r3 = parse_exec_result(e3_out, "/bin/hello")
    r4 = parse_exec_result(e4_out, "/bin/args")
    r5 = parse_exec_result(e5_out, "/bin/hello")

    print(f"EXEC_1 (/bin/hello):    PID={r1['pid']} EXIT={r1['exit_status']} REAP={r1['reaped']} PROMPT={r1['prompt']}")
    print(f"EXEC_2 (/bin/args a b c): PID={r2['pid']} EXIT={r2['exit_status']} REAP={r2['reaped']} PROMPT={r2['prompt']}")
    print(f"EXEC_3 (/bin/hello):    PID={r3['pid']} EXIT={r3['exit_status']} REAP={r3['reaped']} PROMPT={r3['prompt']}")
    print(f"EXEC_4 (/bin/args x y): PID={r4['pid']} EXIT={r4['exit_status']} REAP={r4['reaped']} PROMPT={r4['prompt']}")
    print(f"EXEC_5 (/bin/hello):    PID={r5['pid']} EXIT={r5['exit_status']} REAP={r5['reaped']} PROMPT={r5['prompt']}")

    h1_ok = ("hello from XNU-XZS userland" in e1_out and r1['exit_status'] == "0" and r1['reaped'] == "yes")
    h3_ok = ("hello from XNU-XZS userland" in e3_out and r3['exit_status'] == "0" and r3['reaped'] == "yes")
    h5_ok = ("hello from XNU-XZS userland" in e5_out and r5['exit_status'] == "0" and r5['reaped'] == "yes")

    a2_ok = ("argc=" in e2_out and "4" in e2_out and all(a in e2_out for a in ["/bin/args", "a", "b", "c"]) and r2['exit_status'] == "0" and r2['reaped'] == "yes")
    a4_ok = ("argc=" in e4_out and "3" in e4_out and all(a in e4_out for a in ["/bin/args", "x", "y"]) and r4['exit_status'] == "0" and r4['reaped'] == "yes")

    pwd_pre_ok = ("/" in pwd_pre and "xzs# " in pwd_pre)
    pwd_post_ok = ("/" in pwd_post and "xzs# " in pwd_post)
    has_panic = "panic" in log_text.lower() or "debugger called" in log_text.lower()

    run_pass = (h1_ok and a2_ok and h3_ok and a4_ok and h5_ok and
                pwd_pre_ok and pwd_post_ok and not has_panic)
    print(f"EXEC_1_OK={'yes' if h1_ok else 'no'}")
    print(f"EXEC_2_OK={'yes' if a2_ok else 'no'}")
    print(f"EXEC_3_OK={'yes' if h3_ok else 'no'}")
    print(f"EXEC_4_OK={'yes' if a4_ok else 'no'}")
    print(f"EXEC_5_OK={'yes' if h5_ok else 'no'}")
    print(f"FINAL_PWD={'/' if pwd_post_ok else 'FAIL'}")
    print(f"PANIC={'yes' if has_panic else 'no'}")
    print(f"MIXED_RUN_{run_num}_VERDICT={'PASS' if run_pass else 'FAIL'}")


def main():
    mode = "t2n6a"
    run_num = "1"
    skip_boot = "--no-boot" in sys.argv

    for arg in sys.argv:
        if arg.startswith("--mode="):
            mode = arg.split("=")[1]
        elif arg.startswith("--run="):
            run_num = arg.split("=")[1]

    rev_res = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], capture_output=True, text=True)
    short_commit = rev_res.stdout.strip() if rev_res.returncode == 0 else "unknown"

    if not skip_boot:
        boot_desc = f"{mode}_run{run_num}" if mode == "mixed" else "t2n6a"
        boot_device(boot_desc)

    dev, initial_log = connect_console(skip_boot=skip_boot)

    if mode == "t2n6a":
        run_t2n6a_mode(dev, initial_log, short_commit)
    elif mode == "mixed":
        run_mixed_mode(dev, initial_log, short_commit, run_num)
    else:
        print(f"Unknown mode: {mode}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
