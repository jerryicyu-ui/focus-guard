# Render an HTML file in docs/ to a PDF in submission/ with Chrome or Edge.
#   default: docs/proposal.html -> submission/Week1_Proposal.pdf
#   week 2:  -Source progress_week2.html -Output Week2_Progress.pdf
# Keep this file ASCII-only: Windows PowerShell 5.1 reads BOM-less UTF-8 as the ANSI code page.
param(
    [string]$Source = 'proposal.html',
    [string]$Output = 'Week1_Proposal.pdf'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'submission'
New-Item -ItemType Directory -Force $out | Out-Null

$browser = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe",
    "$env:LOCALAPPDATA\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Microsoft\Edge\Application\msedge.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $browser) { throw 'Chrome or Edge is required to build the PDF.' }

$tempDir = Join-Path $env:TEMP 'focusguard_proposal'
New-Item -ItemType Directory -Force $tempDir | Out-Null
$tempPdf = Join-Path $tempDir 'proposal.pdf'
if (Test-Path $tempPdf) { Remove-Item $tempPdf }

$url = ([Uri](Join-Path $root (Join-Path 'docs' $Source))).AbsoluteUri
$chromeArgs = @('--headless=new', '--disable-gpu', '--no-pdf-header-footer', "--user-data-dir=$tempDir\profile",
    "--print-to-pdf=$tempPdf", $url)
Start-Process $browser -ArgumentList $chromeArgs -Wait -NoNewWindow
if (-not (Test-Path $tempPdf)) { throw 'PDF was not generated.' }
Copy-Item $tempPdf (Join-Path $out $Output) -Force
Write-Host "PDF -> submission\$Output"
