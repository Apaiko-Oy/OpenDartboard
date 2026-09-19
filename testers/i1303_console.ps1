# unrun-tester: copied to a Windows staging directory and driven by i1303_windows.sh, which
# carries its own marker and is run by hand. It needs a real Windows console and a Windows
# build of the launcher, for i1258_console.ps1's reason: the rule under test is that the
# question is asked only of a console input buffer, so nothing that could be stubbed here
# would be testing it.
# #1303: drive the launcher in a REAL Windows console and measure that the window stays.
#
# #1258's i1303 sibling, cut down. The shape is the same and for the same reason: the
# launcher holds the window open by reading a line, and a line can only be read from a
# console input buffer -- so a pipe or a file cannot drive it, because that is the rule
# being measured. This starts the launcher in a new hidden console of its own, attaches to
# that console, and then asks the one question this slice turns on:
#
#   WHEN THE ENDING HAS BEEN PRINTED, IS THE LAUNCHER STILL RUNNING?
#
# That is what "leaves a window on screen" means, and nothing about the text on the screen
# proves it. A launcher that printed the fault and exited would look identical in a
# captured screen and would be exactly the fault this slice exists to remove.
#
# Then it types Enter and records the exit code, which is the fifth criterion.
#
# The process is stopped by the pid Start-Process returned, never by name.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File i1303_console.ps1 `
#     -Exe C:\od-build\i1303\opendartboard-launcher.exe -WorkDir C:\od-run\i1303 -Label clean `
#     -Detector C:\od-build\i1303\stub.exe -StubExit 0 -Await "ended cleanly" -TypeEnter
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$WorkDir,
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$ArgLine = "",
    [string]$Detector = "",
    [string]$StubExit = "",
    [string]$StubMode = "",
    [string]$ArgvTo = "",
    [string]$Await = "",
    [int]$TimeoutSec = 40,
    [switch]$TypeEnter
)
$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class Od1303 {
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool FreeConsole();
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool AttachConsole(uint pid);
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern IntPtr CreateFileW(string name, uint access, uint share, IntPtr sa, uint disp, uint flags, IntPtr tmpl);
  [DllImport("kernel32.dll")] public static extern bool CloseHandle(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct COORD { public short X; public short Y; }
  [StructLayout(LayoutKind.Sequential)] public struct SMALL_RECT { public short Left; public short Top; public short Right; public short Bottom; }
  [StructLayout(LayoutKind.Sequential)] public struct CSBI { public COORD dwSize; public COORD dwCursorPosition; public ushort wAttributes; public SMALL_RECT srWindow; public COORD dwMaximumWindowSize; }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetConsoleScreenBufferInfo(IntPtr h, out CSBI info);
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool SetConsoleScreenBufferSize(IntPtr h, COORD size);
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern bool ReadConsoleOutputCharacterW(IntPtr h, [Out] char[] buf, uint len, COORD at, out uint read);
  [StructLayout(LayoutKind.Explicit, CharSet=CharSet.Unicode)] public struct KEY_EVENT_RECORD {
    [FieldOffset(0)] public int bKeyDown; [FieldOffset(4)] public ushort wRepeatCount;
    [FieldOffset(6)] public ushort wVirtualKeyCode; [FieldOffset(8)] public ushort wVirtualScanCode;
    [FieldOffset(10)] public char UnicodeChar; [FieldOffset(12)] public uint dwControlKeyState; }
  [StructLayout(LayoutKind.Explicit)] public struct INPUT_RECORD { [FieldOffset(0)] public ushort EventType; [FieldOffset(4)] public KEY_EVENT_RECORD KeyEvent; }
  [DllImport("kernel32.dll", SetLastError=true)] public static extern bool WriteConsoleInputW(IntPtr h, INPUT_RECORD[] recs, uint len, out uint written);

  const uint GENERIC_READ = 0x80000000, GENERIC_WRITE = 0x40000000, SHARE_RW = 3, OPEN_EXISTING = 3;
  public static IntPtr Out() { return CreateFileW("CONOUT$", GENERIC_READ | GENERIC_WRITE, SHARE_RW, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero); }
  public static IntPtr In()  { return CreateFileW("CONIN$",  GENERIC_READ | GENERIC_WRITE, SHARE_RW, IntPtr.Zero, OPEN_EXISTING, 0, IntPtr.Zero); }

  public static string Screen(IntPtr h) {
    CSBI info; if (!GetConsoleScreenBufferInfo(h, out info)) return "";
    int w = info.dwSize.X; int rows = info.dwCursorPosition.Y + 1;
    var sb = new System.Text.StringBuilder();
    char[] row = new char[w];
    for (int y = 0; y < rows; y++) {
      uint read; COORD at = new COORD(); at.X = 0; at.Y = (short)y;
      ReadConsoleOutputCharacterW(h, row, (uint)w, at, out read);
      sb.Append(new string(row, 0, (int)read).TrimEnd()).Append('\n');
    }
    return sb.ToString();
  }

  public static void Type(IntPtr h, string text) {
    string all = text + "\r";
    var recs = new INPUT_RECORD[all.Length * 2];
    for (int i = 0; i < all.Length; i++) {
      char c = all[i];
      for (int d = 0; d < 2; d++) {
        var r = new INPUT_RECORD(); r.EventType = 1;
        r.KeyEvent.bKeyDown = d == 0 ? 1 : 0; r.KeyEvent.wRepeatCount = 1;
        r.KeyEvent.wVirtualKeyCode = (ushort)(c == '\r' ? 0x0D : 0);
        r.KeyEvent.UnicodeChar = c;
        recs[i * 2 + d] = r;
      }
    }
    uint written; WriteConsoleInputW(h, recs, (uint)recs.Length, out written);
  }
}
"@

New-Item -ItemType Directory -Force -Path $WorkDir | Out-Null

# Every control the stub detector reads comes from here, so argv stays the thing under test.
if ($Detector -ne "") { $env:OD_DETECTOR = $Detector } else { Remove-Item Env:OD_DETECTOR -ErrorAction SilentlyContinue }
if ($StubExit -ne "") { $env:STUB_EXIT = $StubExit } else { Remove-Item Env:STUB_EXIT -ErrorAction SilentlyContinue }
if ($StubMode -ne "") { $env:STUB_MODE = $StubMode } else { Remove-Item Env:STUB_MODE -ErrorAction SilentlyContinue }
if ($ArgvTo -ne "")   { $env:STUB_ARGV_TO = $ArgvTo } else { Remove-Item Env:STUB_ARGV_TO -ErrorAction SilentlyContinue }

$log = Join-Path $WorkDir "$Label.driver.txt"
if (Test-Path $log) { Remove-Item $log }
function Note($s) { $line = "$(Get-Date -Format HH:mm:ss.fff) $s"; Add-Content -Path $log -Value $line; Write-Output $line }

$started = Get-Date
function Deadline() { ((Get-Date) - $started).TotalSeconds -gt $TimeoutSec }

if ($ArgLine -ne "") {
    $p = Start-Process -FilePath $Exe -ArgumentList $ArgLine -WorkingDirectory $WorkDir -WindowStyle Hidden -PassThru
} else {
    $p = Start-Process -FilePath $Exe -WorkingDirectory $WorkDir -WindowStyle Hidden -PassThru
}
Note "STARTED pid=$($p.Id) args=[$ArgLine]"

[void][Od1303]::FreeConsole()
$attached = $false
for ($i = 0; $i -lt 200 -and -not $attached; $i++) {
    $attached = [Od1303]::AttachConsole([uint32]$p.Id)
    if (-not $attached) { Start-Sleep -Milliseconds 50 }
}
if (-not $attached) {
    # A process that has already exited has no console left to attach to. That is itself
    # an answer -- it did not stay -- so it is recorded rather than thrown.
    $p.WaitForExit()
    Set-Content -Path (Join-Path $WorkDir "$Label.result.txt") -Value @(
        "ATTACHED=0", "AWAIT_SEEN=0", "STILL_RUNNING_AFTER_REPORT=0", "EXIT_CODE=$($p.ExitCode)")
    Note "ATTACH_FAILED exit=$($p.ExitCode)"
    exit 0
}
$out = [Od1303]::Out(); $in = [Od1303]::In()
$size = New-Object Od1303+COORD; $size.X = 160; $size.Y = 9999
[void][Od1303]::SetConsoleScreenBufferSize($out, $size)

$seen = $false
$stillRunning = $false
while (-not $seen) {
    $screen = [Od1303]::Screen($out)
    if ($Await -eq "" -or $screen.Contains($Await)) {
        $seen = $true
        # THE MEASUREMENT. The ending is on the screen; is the launcher still there?
        $stillRunning = -not $p.HasExited
        break
    }
    if ($p.HasExited) { Note "EXITED before [$Await] appeared"; break }
    if (Deadline) { Note "TIMEOUT waiting for [$Await]"; break }
    Start-Sleep -Milliseconds 100
}
Note "AWAIT_SEEN=$([int]$seen) STILL_RUNNING_AFTER_REPORT=$([int]$stillRunning)"

if ($TypeEnter -and -not $p.HasExited) {
    Start-Sleep -Milliseconds 300
    Note "TYPE Enter"
    [Od1303]::Type($in, "")
}

$exited = $p.WaitForExit(($TimeoutSec * 1000))
$screen = [Od1303]::Screen($out)
[System.IO.File]::WriteAllText((Join-Path $WorkDir "$Label.screen.txt"), $screen, (New-Object System.Text.UTF8Encoding $false))

$code = ""
if ($exited) { $code = $p.ExitCode } else { Note "DID_NOT_EXIT after Enter"; Stop-Process -Id $p.Id -Force }

Set-Content -Path (Join-Path $WorkDir "$Label.result.txt") -Value @(
    "ATTACHED=1",
    "AWAIT_SEEN=$([int]$seen)",
    "STILL_RUNNING_AFTER_REPORT=$([int]$stillRunning)",
    "EXITED=$([int]$exited)",
    "EXIT_CODE=$code")

[void][Od1303]::CloseHandle($out); [void][Od1303]::CloseHandle($in)
[void][Od1303]::FreeConsole()
Note "SCREEN $Label.screen.txt EXIT_CODE=$code"
