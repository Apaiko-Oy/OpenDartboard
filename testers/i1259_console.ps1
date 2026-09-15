# #1259: start the detector the way a double-click does -- no command line, a console of its
# own -- from a folder that is NOT the .exe's, and type into that real console from a script.
#
# #1258's i1258_console.ps1 shape (a hidden console of its own, attached by pid, keystrokes as
# console input events, the screen saved as a person saw it), with two differences a prompted
# pairing needs:
#   * The answers arrive over time. A code is minted only when it is needed (a club code after
#     a revocation cannot exist at start), so answers are read from -AnswersFile as they are
#     appended: one line per answer, "<text on screen to wait for><TAB><what to type>". The
#     k-th answer waiting for a needle is typed once the needle has appeared k times.
#   * The run is long, so the screen is written to <Label>.screen.txt every second and the run
#     ends when -StopFile appears, the program exits, or -TimeoutSec passes.
#
# The address comes from the environment, as a double-click's would: OD_TURNAUS_URL and
# OD_ALLOW_PLAINTEXT=1. APPDATA is pointed at a folder of this run's own, so the credential
# and cameras.json this run writes are nobody else's. The process is stopped by the pid
# Start-Process returned, never by name.
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$WorkDir,
    [Parameter(Mandatory = $true)][string]$Label,
    [Parameter(Mandatory = $true)][string]$AnswersFile,
    [Parameter(Mandatory = $true)][string]$StopFile,
    [string]$AppData = "",
    [string]$TurnausUrl = "",
    [string]$Extra = "",
    [int]$TimeoutSec = 600
)
$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public static class OdCon59 {
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
if ($AppData -ne "") { New-Item -ItemType Directory -Force -Path $AppData | Out-Null; $env:APPDATA = $AppData }
if ($TurnausUrl -ne "") { $env:OD_TURNAUS_URL = $TurnausUrl; $env:OD_ALLOW_PLAINTEXT = "1" }
if ($Extra -ne "") { $env:OD_EXTRA_SOURCES = $Extra } else { Remove-Item Env:OD_EXTRA_SOURCES -ErrorAction SilentlyContinue }
Remove-Item Env:OD_MAX_CYCLES -ErrorAction SilentlyContinue

$log = Join-Path $WorkDir "$Label.driver.txt"
function Note($s) { $line = "$(Get-Date -Format HH:mm:ss.fff) $s"; Add-Content -Path $log -Value $line; Write-Output $line }

$started = Get-Date
# No -ArgumentList: a double-click passes no command line.
$p = Start-Process -FilePath $Exe -WorkingDirectory $WorkDir -WindowStyle Hidden -PassThru
Set-Content -Path (Join-Path $WorkDir "$Label.pid") -Value $p.Id
Note "STARTED pid=$($p.Id) cwd=$WorkDir exe=$Exe (no arguments)"

[void][OdCon59]::FreeConsole()
$attached = $false
for ($i = 0; $i -lt 100 -and -not $attached; $i++) {
    $attached = [OdCon59]::AttachConsole([uint32]$p.Id)
    if (-not $attached) { Start-Sleep -Milliseconds 50 }
}
if (-not $attached) { Note "ATTACH_FAILED"; Stop-Process -Id $p.Id -Force; exit 3 }
$out = [OdCon59]::Out(); $in = [OdCon59]::In()
$size = New-Object OdCon59+COORD; $size.X = 200; $size.Y = 30000
[void][OdCon59]::SetConsoleScreenBufferSize($out, $size)

function Count($text, $needle) { ([regex]::Matches($text, [regex]::Escape($needle))).Count }
$screenPath = Join-Path $WorkDir "$Label.screen.txt"
$utf8 = New-Object System.Text.UTF8Encoding $false
$typed = 0
$lastSave = Get-Date "2000-01-01"

while ($true) {
    $screen = [OdCon59]::Screen($out)
    if (((Get-Date) - $lastSave).TotalMilliseconds -ge 1000) {
        [System.IO.File]::WriteAllText($screenPath, [regex]::Replace($screen, "[\x1b←]\[[0-9;]*m", ""), $utf8)
        $lastSave = Get-Date
    }
    if (Test-Path $StopFile) { Note "STOP_FILE"; break }
    if ($p.HasExited) { Note "EXITED code=$($p.ExitCode)"; break }
    if (((Get-Date) - $started).TotalSeconds -gt $TimeoutSec) { Note "TIMEOUT"; break }

    $lines = @()
    if (Test-Path $AnswersFile) { $lines = @(Get-Content -Path $AnswersFile | Where-Object { $_ -ne "" }) }
    if ($typed -lt $lines.Count) {
        $parts = $lines[$typed] -split "`t", 2
        $needle = $parts[0]; $answer = $parts[1]
        $seenBefore = 0
        for ($k = 0; $k -lt $typed; $k++) { if ((($lines[$k] -split "`t", 2)[0]) -eq $needle) { $seenBefore++ } }
        if ((Count $screen $needle) -ge ($seenBefore + 1)) {
            Start-Sleep -Milliseconds 400
            Note "TYPE #$($typed + 1) after [$needle]"
            [OdCon59]::Type($in, $answer)
            $typed++
        }
    }
    Start-Sleep -Milliseconds 250
}

$screen = [OdCon59]::Screen($out)
[System.IO.File]::WriteAllText($screenPath, [regex]::Replace($screen, "[\x1b←]\[[0-9;]*m", ""), $utf8)
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Note "STOPPED pid=$($p.Id)" }
[void][OdCon59]::CloseHandle($out); [void][OdCon59]::CloseHandle($in)
[void][OdCon59]::FreeConsole()
Note "DONE typed=$typed"
