# Live check of the agent gateway, end to end.
#
# The SSE framing can only be held to what the gateway actually sends by talking
# to it, so this drives rpa-ai-probe through the cases that break differently:
# auth, payload size, a wrong key, a wrong path, a short timeout. The happy-path
# stream is recorded to disk; copy that recording into tests/data/ and the live
# check becomes an offline regression test.
#
# Credentials come from RPA_AI_API_KEY, or from the Windows Credential Manager
# entry the desktop app already wrote. The key is never printed.
#
# KEEP THIS FILE UTF-8 *WITH BOM*. Windows PowerShell 5.1 reads a BOM-less .ps1
# as ANSI, so the Chinese prompts below arrive at the gateway as mojibake -- and
# nothing fails: the agent simply answers a question nobody asked, which reads
# as a bad model rather than a broken script.
#
#   powershell -ExecutionPolicy Bypass -File tools\verify-agent-gateway.ps1
#
[CmdletBinding()]
param(
    [string] $Probe,
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'

# Resolved here rather than in the param defaults: $PSScriptRoot is not reliably
# populated while those are being evaluated.
$repoRoot = Split-Path -Parent $PSCommandPath | Split-Path -Parent
if (-not $Probe) { $Probe = Join-Path $repoRoot 'build-full\bin\rpa-ai-probe.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build-full\gateway-check' }

if (-not (Test-Path $Probe)) {
    Write-Error "rpa-ai-probe not found at $Probe. Run build-full.cmd first."
}

function Get-StoredGatewayKey {
    # The desktop app keeps the key in the Credential Manager rather than the
    # registry, so reading it needs the Win32 call.
    if (-not ('Rpa.CredentialReader' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace Rpa {
    public static class CredentialReader {
        [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CredReadW(string target, uint type, uint flags, out IntPtr credential);

        [DllImport("advapi32.dll")]
        private static extern void CredFree(IntPtr buffer);

        [StructLayout(LayoutKind.Sequential)]
        private struct CREDENTIAL {
            public uint Flags;
            public uint Type;
            public IntPtr TargetName;
            public IntPtr Comment;
            public System.Runtime.InteropServices.ComTypes.FILETIME LastWritten;
            public uint CredentialBlobSize;
            public IntPtr CredentialBlob;
            public uint Persist;
            public uint AttributeCount;
            public IntPtr Attributes;
            public IntPtr TargetAlias;
            public IntPtr UserName;
        }

        public static string Read(string target) {
            IntPtr raw;
            if (!CredReadW(target, 1, 0, out raw)) return null;
            try {
                CREDENTIAL c = (CREDENTIAL)Marshal.PtrToStructure(raw, typeof(CREDENTIAL));
                if (c.CredentialBlobSize == 0) return null;
                byte[] blob = new byte[c.CredentialBlobSize];
                Marshal.Copy(c.CredentialBlob, blob, 0, (int)c.CredentialBlobSize);
                return System.Text.Encoding.UTF8.GetString(blob);
            } finally {
                CredFree(raw);
            }
        }
    }
}
'@
    }

    foreach ($target in @('RPA-Block/ai-gateway', 'PRA-compiler/ai-gateway')) {
        $secret = [Rpa.CredentialReader]::Read($target)
        if ($secret) { return $secret }
    }
    return $null
}

if (-not $env:RPA_AI_API_KEY) {
    $stored = Get-StoredGatewayKey
    if (-not $stored) {
        Write-Error "No credentials. Set RPA_AI_API_KEY or configure the gateway key in the app."
    }
    $env:RPA_AI_API_KEY = $stored
    Write-Host "using the key stored by the desktop app" -ForegroundColor DarkGray
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null

$failures = @()
function Invoke-Case {
    param([string] $Name, [string[]] $Arguments, [int] $ExpectedExit)

    Write-Host ""
    Write-Host "== $Name" -ForegroundColor Cyan
    $log = Join-Path $OutputDirectory ("{0}.log" -f ($Name -replace '[^\w\-]', '-'))

    # Half these cases are *supposed* to fail, and they report on stderr. With
    # the script's Stop preference in force, PowerShell wraps each stderr line
    # in an ErrorRecord and aborts the run at the first expected failure.
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $Probe @Arguments 2>&1 | Tee-Object -FilePath $log | Out-Host
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }

    if ($code -eq $ExpectedExit) {
        Write-Host "   pass (exit $code)" -ForegroundColor Green
    } else {
        Write-Host "   FAIL (exit $code, expected $ExpectedExit)" -ForegroundColor Red
        $script:failures += $Name
    }
}

$sseDump = Join-Path $OutputDirectory 'happy-path.sse'
$sampleFlow = Get-ChildItem -Path (Join-Path $repoRoot 'examples') -Filter '*.rpa.json' `
    -ErrorAction SilentlyContinue | Select-Object -First 1

Invoke-Case -Name 'reachable' -ExpectedExit 0 -Arguments @('test')

Invoke-Case -Name 'authors-a-flow' -ExpectedExit 0 -Arguments @(
    'chat', '開啟記事本，輸入「哈囉」，然後存檔', '--dump-sse', $sseDump)

if ($sampleFlow) {
    Invoke-Case -Name 'edits-an-existing-flow' -ExpectedExit 0 -Arguments @(
        'chat', '在最後面加一個等待兩秒的步驟', '--flow', $sampleFlow.FullName)
}

# A wrong key must come back with the gateway's own words, not just Qt's
# "the host requires authentication" -- that text is what tells a user whether
# the key or the request was the problem.
Invoke-Case -Name 'rejects-a-bad-key' -ExpectedExit 1 -Arguments @(
    'test', '--api-key', 'sk-definitely-not-valid')

Invoke-Case -Name 'reports-a-wrong-path' -ExpectedExit 1 -Arguments @(
    'test', '--gateway', 'https://agents.scfg.io/no-such-agent')

Invoke-Case -Name 'reports-a-timeout' -ExpectedExit 1 -Arguments @(
    'chat', '寫一個很長的流程', '--timeout', '1500')

Write-Host ""
if ($failures.Count -eq 0) {
    Write-Host "all cases behaved as expected" -ForegroundColor Green
    if (Test-Path $sseDump) {
        Write-Host "raw stream: $sseDump" -ForegroundColor DarkGray
        Write-Host "copy it into tests/data/ to lock the framing down offline" -ForegroundColor DarkGray
    }
    exit 0
}

Write-Host ("unexpected: {0}" -f ($failures -join ', ')) -ForegroundColor Red
Write-Host "logs in $OutputDirectory" -ForegroundColor DarkGray
exit 1
