# #1258: drive the detector's camera question in a REAL Windows console, from a script.
#
# The question is only asked when the program's input is a console input buffer
# (console_prompt::isInteractiveConsole), so a pipe or a file cannot drive it: that is the
# rule being tested. This starts the .exe in a new, hidden console window of its own,
# attaches to that console, waits until the screen shows the question, types each answer
# as key events, and at the end saves what the console screen showed -- the prompt, the
# list, the refusals and the typed answers as a person would have seen them.
#
# The process is stopped by the pid Start-Process returned, never by name.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File i1258_console.ps1 `
#     -Exe C:\od-build\<sha>\build-win-static\opendartboard.exe -WorkDir C:\od-run\i1258\p1 `
#     -Label p1 -ArgLine "--turnaus http://127.0.0.1:9 --allow-plaintext" `
#     -Answers "x","1 1 2","1 2 3" -Final "CAMERAS:" -AppData C:\od-run\i1258\appdata
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$WorkDir,
    [Parameter(Mandatory = $true)][string]$Label,
    [string]$ArgLine = "",
    [string[]]$Answers = @(),
    [string]$Question = "press Enter:",
    [string]$Final = "CAMERAS:",
    [int]$SettleSec = 8,
    [int]$TimeoutSec = 120,
    [string]$AppData = "",
    [string]$Extra = "",
    [string]$MaxCycles = ""
)
$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class OdCon {
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
if ($AppData -ne "") { $env:APPDATA = $AppData }
if ($Extra -ne "") { $env:OD_EXTRA_SOURCES = $Extra } else { Remove-Item Env:OD_EXTRA_SOURCES -ErrorAction SilentlyContinue }
if ($MaxCycles -ne "") { $env:OD_MAX_CYCLES = $MaxCycles } else { Remove-Item Env:OD_MAX_CYCLES -ErrorAction SilentlyContinue }

$log = Join-Path $WorkDir "$Label.driver.txt"
function Note($s) { $line = "$(Get-Date -Format HH:mm:ss.fff) $s"; Add-Content -Path $log -Value $line; Write-Output $line }

$started = Get-Date
$p = Start-Process -FilePath $Exe -ArgumentList $ArgLine -WorkingDirectory $WorkDir -WindowStyle Hidden -PassThru
Set-Content -Path (Join-Path $WorkDir "$Label.pid") -Value $p.Id
Note "STARTED pid=$($p.Id) args=[$ArgLine]"

[void][OdCon]::FreeConsole()
$attached = $false
for ($i = 0; $i -lt 100 -and -not $attached; $i++) {
    $attached = [OdCon]::AttachConsole([uint32]$p.Id)
    if (-not $attached) { Start-Sleep -Milliseconds 50 }
}
if (-not $attached) { Note "ATTACH_FAILED"; Stop-Process -Id $p.Id -Force; exit 3 }
$out = [OdCon]::Out(); $in = [OdCon]::In()
$size = New-Object OdCon+COORD; $size.X = 160; $size.Y = 9999
[void][OdCon]::SetConsoleScreenBufferSize($out, $size)

function Count($text, $needle) { ([regex]::Matches($text, [regex]::Escape($needle))).Count }
function Deadline() { ((Get-Date) - $started).TotalSeconds -gt $TimeoutSec }

$n = 0
foreach ($answer in $Answers) {
    $n++
    while ((Count ([OdCon]::Screen($out)) $Question) -lt $n) {
        if ($p.HasExited) { Note "EXITED before question $n"; break }
        if (Deadline) { Note "TIMEOUT waiting for question $n"; break }
        Start-Sleep -Milliseconds 200
    }
    Start-Sleep -Milliseconds 300
    Note "TYPE [$answer]"
    [OdCon]::Type($in, $answer)
}

if ($Final -ne "") {
    while ((Count ([OdCon]::Screen($out)) $Final) -lt 1) {
        if ($p.HasExited) { Note "EXITED before final"; break }
        if (Deadline) { Note "TIMEOUT waiting for final [$Final]"; break }
        Start-Sleep -Milliseconds 200
    }
    Note "FINAL seen [$Final]"
}
Start-Sleep -Seconds $SettleSec

$screen = [OdCon]::Screen($out)
$clean = [regex]::Replace($screen, "[\x1b\u2190]\[[0-9;]*m", "")
[System.IO.File]::WriteAllText((Join-Path $WorkDir "$Label.screen.txt"), $clean, (New-Object System.Text.UTF8Encoding $false))

if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Note "STOPPED pid=$($p.Id)" } else { Note "EXIT_CODE=$($p.ExitCode)" }
[void][OdCon]::CloseHandle($out); [void][OdCon]::CloseHandle($in)
[void][OdCon]::FreeConsole()
Note "SCREEN $Label.screen.txt questions=$(Count $clean $Question)"
