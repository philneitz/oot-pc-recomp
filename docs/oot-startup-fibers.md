# Startup failures and cross-thread fibers

## Reproduction

The unmodified executable intermittently failed during startup. Six bounded starts reproduced three process
failures: a write into SDK code, an invalid-stack exception, and an SDK abort followed by an access violation.
Every run used a fresh disposable profile. Three starts reached the 25-second watchdog without a reported fault;
the watchdog termination is not a normal game exit.

Disabling asynchronous preemption (`TOTK_ASYNC_PREEMPT=2`) did not eliminate the SDK write fault. Disabling
implicit Vulkan layers did not eliminate the native execution faults either. Neither switch is a demonstrated
fix. Running under a debugger changed the timing enough that six 20-second runs did not reproduce the failure.

## What the crash reports missed

The Windows first-chance reporter only accepted instruction addresses inside the executable. Some failing native
returns went into private heap memory, so Windows reported an access violation while the runtime log ended
without a fatal line. The reporter now also registers a final unhandled-exception fallback, after other handlers
have had their chance. Reports include native registers and stack words, and identify the actual module instead
of always printing an executable-relative offset. Failed guest dispatches also print the Cpu passed to the branch.

The newly captured native stack pointed back through fiber switching. The published guest Cpu in these reports
could still be the base thread's idle context, so it was insufficient to diagnose the executing fiber by itself.

## Repair

`runtime/kernel.c` supports resuming a suspended native fiber on another OS thread. A saved host call therefore
cannot keep using the thread pointer from before its switch:

- `fiber_switch_to` restored `host_depth` through its old `RtThread *`.
- `fiber_main` used thread-local state across a guest call that could migrate, including when the fiber returned.
- `runtime/hostfn.c` `hostfn_tracked` decremented the old thread's depth after the wrapped call migrated.

The switch path now reloads the current thread through a non-inlined TLS accessor with a compiler barrier, and
restores depth on that thread. Host-call and SVC wrappers reacquire the owner from the resumed Cpu before finishing
their accounting. This avoids retaining compiler-cached TLS addresses across suspension as well as retaining the
old owner explicitly.

## Regression coverage

`runtime/kernel_probe.c` adds a migrating fiber to `--probe-kernel`. Two guest threads alternate 40 times, checking
the current Cpu, owner, thread-local region, native thread ID and host-call depth. Both original threads then end,
and the main thread resumes the fiber once more so its function can return. Before the fix this test crashed; with
the fix it completes with zero context/depth mismatches. The existing 10,000 same-thread switches and ExitThread
unwinding tests also pass.

The probe had an older invalid-enum assertion for WaitForAddress type 3. The Switch 2 runtime already implements
type 3 as WaitIfEqual64, so the assertion was incorrect before these changes. It now checks the matching and
nonmatching upper 32 bits, and uses type 4 for the invalid wait enum. The complete kernel probe and selftest pass.

Sixteen post-fix startup windows (ten of 15 seconds and six of 25 seconds) completed without a reported failure,
with the ordinary Vulkan layer and preemption configuration restored. These are bounded startup checks, not
evidence of long-session stability.

Three subsequent copied-save gameplay runs also exited normally after 120 game-clock seconds each (about
126-144 wall-clock seconds). One included the inherited-uniform snapshot repair described in
[the camera investigation](oot-camera-snapshots.md).

Private evidence is under `build/test/startup-study-*`, with the before/after synthetic probe logs under
`build/test/kernel-migration-*`. The baseline executable is retained as
`build/oot111/oot-startup-baseline.exe`. No user save was changed by these startup experiments.
