# ExtraBytes

A Windows 10 x64 proof-of-concept that achieves **cross-process read, write, and code execution** by abusing the Edit control's extra window bytes (`cbWndExtra`).

**Target:** Windows 10 22H2 (build 19045), x64

## Background

The concept of abusing window messages for cross-process interaction dates back to [shatter attacks](https://en.wikipedia.org/wiki/Shatter_attack) (Chris Paget, 2002). Since then, several techniques have built on this foundation:

- **PowerLoader** (~2013) introduced Extra Window Memory injection via `Shell_TrayWnd`, modifying extra bytes to redirect function pointers in `explorer.exe`. This was codified as [MITRE T1055.011](https://attack.mitre.org/techniques/T1055/011/).
- **WordWarping** ([Hexacorn, 2019](https://www.hexacorn.com/blog/2019/04/23/wordwarper-new-code-injection-trick/)) named the technique of hijacking the Edit control's `EM_SETWORDBREAKPROC` callback for code execution, building on work originally documented by Oliver Lavery (iDefense, 2003).
- **ShatterLoad** ([Pentraze, 2024](https://pentraze.com/vulnerability-reports/PTRZ-2024-0225/)) revived the approach using `EM_SETWORDBREAKPROC` pointed at `LoadLibraryW`, triggered by text wrapping in a multiline Edit control.

## What's different here

The core contribution is turning `EM_GETWORDBREAKPROC` / `EM_SETWORDBREAKPROC` into a general-purpose cross-process read/write primitive.

The Edit control stores a pointer to an internal `tagED` struct at extra bytes offset 0. The wordbreak proc field lives at `tagED + 0x90`. By temporarily setting `extra[0]` to `(target_address - 0x90)` via `SetWindowLongPtrA`, `EM_GETWORDBREAKPROC` reads 8 bytes from the target address, and `EM_SETWORDBREAKPROC` writes 8 bytes to it.

This gives you a full R/W/X chain:

| Capability | Mechanism |
|---|---|
| **Read** | Set `extra[0]` = `target - 0x90`, send `EM_GETWORDBREAKPROC` → returns `*(target)` |
| **Write** | Set `extra[0]` = `target - 0x90`, send `EM_SETWORDBREAKPROC` with value → writes `*(target)` |
| **Execute** | Set Edit text to command, point wordbreak at `msvcrt!_wsystem`, trigger via `WM_LBUTTONDBLCLK` |

## Related work

| Project | Description |
|---|---|
| [BreakingMalware/PowerLoaderEx](https://github.com/BreakingMalware/PowerLoaderEx) | Original EWM injection via `Shell_TrayWnd` (x86/x64) |
| [theevilbit/injection](https://github.com/theevilbit/injection) | Collection of injection techniques including WordWarping, Treepoline, ListPlanting |
| [Pentraze ShatterLoad](https://pentraze.com/vulnerability-reports/PTRZ-2024-0225/) | `EM_SETWORDBREAKPROC` → `LoadLibraryW` via text wrapping |
| [Hexacorn - WordWarper](https://www.hexacorn.com/blog/2019/04/23/wordwarper-new-code-injection-trick/) | Named the wordbreak callback hijack technique |
| [Hexacorn - Treepoline / ListPlanting / etc.](https://www.hexacorn.com/blog/2019/04/24/3-new-code-injection-tricks/) | Additional callback-based injection tricks |
| [MalwareTech - PowerLoader](https://www.malwaretech.com/2013/08/powerloader-injection-something-truly.html) | Write-up on the original PowerLoader EWM technique |

## Disclaimer

This is an educational tool for studying Windows internals. I do not condone, nor advocate for malicious use of this project. This was strictly just a fun project / learning experience for me, while I try to get better at Windows reversal. 
