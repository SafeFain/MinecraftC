# CTest audit repair: local validation

Completed locally on 2026-11-14 in the existing Windows Release build
(Visual Studio 18 2026). The original104 audit judgments and evidence references
remain unchanged in [the ledger](testing-audit-followup.csv).

- Dispositions: 64 repaired; 16 original retained;
  24 reviewed retained contracts; no open, partial or pending rows.
- Full CTest: **116/116 passed**, no CTest skips, **491.36 seconds** wall time,
  including all10 real Vulkan startup/readback tests.
- Additional real two-process Vulkan LAN: base scene and Nether travel/profile
  reconnect passed, each rendering100 joined frames and producing a capture.
- Release ALL and the normally excluded Vulkan LAN target built successfully.
- Registered metadata: fast94, slow22, assets4, worldgen5, integration13, Vulkan10.
  Every invocation has a timeout, isolated writable TEMP and a fast/slow label.
- API/pixel coverage controls run in the suite. Two compiled subscription identity
  mutants were independently rejected during the earlier unchanged component stage.
- Workflow actionlint, source-preservation comparison, diff and task-state checks
  passed. Existing conversion/shadowing compiler warnings remain; no warning-free
  claim. Source preservation compares tracked source outside the four declared
  session composition/lifecycle files against the closure-authorization checkpoint.

Reproduce the complete suite after configuring/building:

```sh
ctest --test-dir cmake-build-windows -C Release -j4 --output-on-failure --output-junit audit-closure-full-ctest.xml
```

Local evidence: `build-local/audit-closure-final-verified-build.log`,
`build-local/audit-closure-lan-final-build.log`, `build-local/audit-closure-lan-smoke.log`,
`build-local/audit-closure-full-ctest.log`,
`cmake-build-windows/audit-closure-full-ctest.xml`, and
`build-local/audit-closure-ctest-metadata.json`. Failed intermediate fixture/build
attempts remain in the adjacent audit-closure logs and task checkpoints.

These dispositions are not a new A/B/C certification. Retained exact assertions
have specific [contract/fault-model reasons](testing-contract-review.md). Desktop
GPU registration now defaults ON; Linux CI provisions Xvfb/Lavapipe, while
Windows/macOS CI explicitly select CPU/package checks. Remote CI and non-Windows
execution were not performed locally. No staging, commit or push was performed;
pre-existing enchanting/redstone work was preserved and redstone feature work
remains paused under the prior instruction.
