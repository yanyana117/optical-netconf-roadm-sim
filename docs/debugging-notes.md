# Debugging and integration notes

Real issues hit while building this project, in the order they appeared.
Kept because root-cause write-ups are half the value of an integration
project.

## 1. A unit test that was wrong, not the code

First CI run: `Transponder.BerGrowsAsOsnrMarginShrinks` failed. The device
model was correct; the test's final assertion was a meaningless ternary
(`EXPECT_FALSE(alarm ? cleanBer > 0 : false)`) that went true exactly when
the BER model behaved correctly at a 1 dB margin. Rewrote the test as a
three-point monotonicity check (30 dB / 20 dB / 14 dB OSNR) with the alarm
asserted only below the SD-FEC ceiling. Lesson: when a test fails, decide
explicitly whether the spec, the code, or the test is wrong before touching
anything.

## 2. cppcheck cannot parse GoogleTest macros

`cppcheck` reported `syntaxError` on every `TEST(...)` block. This is a
known limitation of cppcheck's parser with gtest macro expansion, not a code
problem. Fix: scope static analysis to `src/`, `include/`, and `netconf/`;
the tests stay covered by the compiler (`-Wall -Wextra`), ctest, coverage,
and valgrind stages. Also fixed the one legitimate finding: the multi-default
constructor `RoadmDevice(int)` needed `explicit`.

## 3. The CESNET version matrix (why the Docker build pins tags)

`sysrepo v3.3.10` refused to configure against `libyang v3.4.2`:

```
CMakeModules/FindLibYANG.cmake:86 (find_package_handle_standard_args)
-- Configuring incomplete, errors occurred!
```

Reading sysrepo's CMakeLists showed `LIBYANG_DEP_VERSION 3.7.5` /
`LIBYANG_DEP_SOVERSION 3.6.7` — the README compatibility tables lag the
actual CMake checks. Bumped to `libyang v3.7.8` (verified against the tag
list) and the stack built. Neither Ubuntu 24.04 nor Debian trixie packages
netopeer2 at all, which is why the image builds the whole
libyang → sysrepo → libnetconf2 → netopeer2 chain from pinned source tags,
the same way real network elements ship it.

## 4. ncclient edit-config: "Missing XML namespace"

Netopeer2 rejected every `<edit-config>` with
`[ERR]: LY: Missing XML namespace. (path "/ietf-netconf:edit-config")`.
When you hand ncclient a *string* for `config`, it forwards it verbatim, so
the `<config>` wrapper element must carry the NETCONF base namespace itself:

```xml
<config xmlns="urn:ietf:params:xml:ns:netconf:base:1.0">...</config>
```

One attribute; the difference between a working management plane and none.

## 5. Background-build exit codes lie through pipes

The first Docker build "succeeded" with exit 0 while the log ended in a
configure error: the build was run as `docker build ... | tail`, and the
pipeline's status is the *tail*'s. Rebuilt with the full log written to a
file and the real exit code echoed. `set -o pipefail` exists for a reason.
